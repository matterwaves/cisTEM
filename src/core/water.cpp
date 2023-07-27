#include "core_headers.h"


const double SOLVENT_DENSITY = 0.94; // 0.94 +/- 0.02 Ghormley JA, Hochanadel CJ. 1971
const double CARBON_DENSITY  = 1.75; // 2.0; // NIST and Holography paper TODO add cite (using the lower density to match the Holography paper)
const double MW_WATER        = 18.01528;
const double MW_CARBON       = 12.0107;
const double CARBON_X_ANG    = 2000.0;
const double CARBON_Y_ANG    = 2000.0;


Water::Water(bool do_carbon)
{
	this->simulate_phase_plate = do_carbon;

}

Water::Water(const PDB *current_specimen, int wanted_size_neighborhood, float wanted_pixel_size, float wanted_dose_per_frame, float max_tilt, float in_plane_rotation, int *padX, int *padY, int nThreads, bool do_carbon)
{

	//
	this->simulate_phase_plate = do_carbon;
	this->Init( current_specimen, wanted_size_neighborhood, wanted_pixel_size, wanted_dose_per_frame, max_tilt, in_plane_rotation, padX, padY, nThreads);


}


Water::~Water()
{
	if (is_allocated_water_coords)
	{
		delete [] water_coords;
	}
}

void Water::Init(const PDB *current_specimen, int wanted_size_neighborhood, float wanted_pixel_size, float wanted_dose_per_frame, float max_tilt, float in_plane_rotation, int *padX, int *padY, int nThreads)
{

	this->size_neighborhood = wanted_size_neighborhood;
	this->pixel_size = wanted_pixel_size;
	this->dose_per_frame = wanted_dose_per_frame;

	this->nThreads = nThreads;

	if (this->simulate_phase_plate)
	{
		this->vol_angX = CARBON_X_ANG;
		this->vol_angY = CARBON_Y_ANG;
		this->vol_angZ = max_tilt;

		this->vol_nX = myroundint(this->vol_angX / wanted_pixel_size);
		this->vol_nY = myroundint(this->vol_angY / wanted_pixel_size);
		this->vol_nZ = myroundint(this->vol_angZ / wanted_pixel_size);
		if (IsEven(this->vol_nZ) == false) this->vol_nZ += 1;
	}
	else
	{

		int padZ;
		vol_nZ = current_specimen->vol_nZ;

		wxPrintf("size pre rot padding %d %d %f rot\n", current_specimen->vol_nX, current_specimen->vol_nY, in_plane_rotation);

		ReturnPadding(max_tilt, in_plane_rotation, current_specimen->vol_nZ, current_specimen->vol_nX, current_specimen->vol_nY, padX, padY, &padZ);

		vol_nX = current_specimen->vol_nX + *padX + padZ; // This assums the tilting is only around the Y-Axis which isn't correct FIXME
		vol_nY = current_specimen->vol_nY + *padY;

		wxPrintf("size post rot padding %d %d padX %d padY %d padZ %d rot\n",vol_nX, vol_nY, *padX, *padY, padZ);


		MyAssertTrue(current_specimen->pixel_size > 0.0f, "The pixel size for your PDB object is not yet set.");
		// Copy over some values from the current specimen - Do these need to be updated for tilts and rotations?
		this->vol_angX = vol_nX * current_specimen->pixel_size; //current_specimen->vol_angX;
		this->vol_angY = vol_nY * current_specimen->pixel_size;
		this->vol_angZ = vol_nZ * current_specimen->pixel_size;

	}

	wxPrintf("vol dimension in Ang %2.2f x %2.2f y  %2.2f z\n", this->vol_angX , this->vol_angY , this->vol_angZ);


	this->vol_oX = floor(this->vol_nX / 2);
	this->vol_oY = floor(this->vol_nY / 2);
	this->vol_oZ = floor(this->vol_nZ / 2);


}

void Water::SeedWaters3d()
{

	// Volume in Ang / (ang^3/nm^3 * nm^3/nWaters) buffer by 10%

	double waters_per_angstrom_cubed;

	if (this->simulate_phase_plate)
	{
		// g/cm^3 * molecules/mole * mole/grams * 1cm^3/10^24 angstrom^3
		waters_per_angstrom_cubed = CARBON_DENSITY * 0.6022140857 / MW_CARBON;
	}
	else
	{
		waters_per_angstrom_cubed = SOLVENT_DENSITY * 0.6022140857 / MW_WATER;
	}

	wxPrintf("Atoms per nm^3 %3.3f, vol (in Ang^3) %2.2f %2.2f %2.2f\n",waters_per_angstrom_cubed*1000,this->vol_angX , this->vol_angY , this->vol_angZ);
	double n_waters_lower_bound = waters_per_angstrom_cubed*(this->vol_angX * this->vol_angY * this->vol_angZ);
	long n_waters_possible = (long)floor(1.05*n_waters_lower_bound);
	wxPrintf("specimen volume is %3.3e nm expecting %3.3e waters\n",(this->vol_angX * this->vol_angY * this->vol_angZ)/1000,n_waters_lower_bound);


    RandomNumberGenerator my_rand(PIf);

	const float random_sigma_cutoff = 1 - (n_waters_lower_bound/double((this->vol_nX - (this->size_neighborhood*this->pixel_size)) *
																	   (this->vol_nY - (this->size_neighborhood*this->pixel_size)) *
																	   (this->vol_nZ - (this->size_neighborhood*this->pixel_size))));
	const float random_sigma_negativo = -1*random_sigma_cutoff;
	float current_random;
	const float random_sigma = 0.5/this->pixel_size; // random shift in pixel values
    float thisRand;
    wxPrintf("cuttoff is %2.6e %2.6e %f %f\n",n_waters_lower_bound,double((this->vol_nX - this->size_neighborhood) *
    																	  (this->vol_nY - this->size_neighborhood) *
    		 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	  (this->vol_nZ - this->size_neighborhood)), random_sigma_cutoff, random_sigma_negativo);



	water_coords = new AtomPos[n_waters_possible];
	is_allocated_water_coords = true;


	//  There are millions to billions of waters. We want to schedule the threads in a way that avoids atomic collisions
	//  Since the updates are in a projected potential, this means we want a given thread to be assigned a block of waters that DO
	//  overlap in Z, which it will handle serially. This is why K is on the inner loop here. To further optimize this, we can also increment the x/y dimensions
	//  in multiples of the neigborhood.

	// Break up the x/y dims into ~ nThreads^2 thread blocks
	int incX = ceil( vol_nX / nThreads);
	int incY = ceil( vol_nY / nThreads);
	int iLower, iUpper, jLower, jUpper,xUpper,yUpper;

	xUpper =this->vol_nX - this->size_neighborhood;
	yUpper =this->vol_nY - this->size_neighborhood;

	for (int i = 0; i < nThreads; i++)
	{
		iLower = i*incX + size_neighborhood;
		iUpper = (1+i)*incX + size_neighborhood;
		for (int j = 0; j < nThreads; j++)
		{
			jLower = j*incY + size_neighborhood;
			jUpper = (1+j)*incY + size_neighborhood;

			for (int k = this->size_neighborhood; k < this->vol_nZ - this->size_neighborhood; k++)
			{
				for (int iInner = iLower; iInner < iUpper; iInner++)
				{
//					if (iInner > xUpper) { continue; }
					for (int jInner = jLower; jInner < jUpper; jInner++)
					{
//						if (jInner > yUpper) { continue; }

						if (  my_rand.GetUniformRandomSTD(0.0,1.0) > random_sigma_cutoff )
						{

							water_coords[number_of_waters].x = (float)iInner;
							water_coords[number_of_waters].y = (float)jInner;
							water_coords[number_of_waters].z = (float)k;

							number_of_waters++;
						}
					}
				}
			}
		}

	}

	wxPrintf("waters added %3.3e (%2.2f%)\n",(float)this->number_of_waters, 100.0f*(float)this->number_of_waters/n_waters_lower_bound);

}

void Water::ShakeWaters3d(int number_of_threads)
{




	float azimuthal;
	float cos_polar;


	const float random_sigma = 1.5f * (dose_per_frame); // TODO check this. The point is to convert an rms displacement in 3D to 2D


	// Try just using the functions output, which may not be quite perfect in distribution, but close.
//    std::random_device rd;  //Will be used to obtain a seed for the random number engine
//    std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
//    std::normal_distribution<float>  norm_dist_mag(0.0,random_sigma*1.5);


	wxPrintf("Using a rmsd of %f for water perturbation\n", random_sigma);


	// Private variables for parfor loop
	float dr, dx, dy, dz;
	float azimuthal_angle = 0;
	float polar_angle = 0;


	// TODO benchmark this
	int local_threads;
	if (number_of_threads > 4)
	{
		local_threads = 4;
	}
	else
	{
		local_threads = number_of_threads;
	}

    RandomNumberGenerator my_rand(local_threads);

    if ((float)number_of_waters < (powf(2,31) - 1) / 3.0f)
    {
		#pragma omp parallel for num_threads(local_threads) private(dr,dx,dy,dz,my_rand)
		for (long iWater = 0; iWater < number_of_waters; iWater++)
		{

			water_coords[iWater].x += my_rand.GetNormalRandomSTD(0.0f,random_sigma);
			water_coords[iWater].y += my_rand.GetNormalRandomSTD(0.0f,random_sigma);
			water_coords[iWater].z += my_rand.GetNormalRandomSTD(0.0f,random_sigma);

			// TODO 2x check that the periodic shifts are doing what they should be.
			// Check boundaries
			if (water_coords[iWater].x < size_neighborhood + 1)
			{
				water_coords[iWater].x = water_coords[iWater].x - 1*size_neighborhood +vol_nX;
			}
			else if (water_coords[iWater].x > vol_nX - size_neighborhood)
			{
				water_coords[iWater].x = water_coords[iWater].x - vol_nX + 1*size_neighborhood;
			}

			// Check boundaries
			if (water_coords[iWater].y < size_neighborhood + 1)
			{
				water_coords[iWater].y = water_coords[iWater].y - 1*size_neighborhood +vol_nY;
			}
			else if (water_coords[iWater].y > vol_nY - size_neighborhood)
			{
				water_coords[iWater].y = water_coords[iWater].y - vol_nY + 1*size_neighborhood;
			}

			// Check boundaries
			if (water_coords[iWater].z < size_neighborhood + 1)
			{
				water_coords[iWater].z = water_coords[iWater].z - 1*size_neighborhood +vol_nZ;
			}
			else if (water_coords[iWater].z > vol_nZ - size_neighborhood)
			{
				water_coords[iWater].z = water_coords[iWater].z - vol_nZ + 1*size_neighborhood;
			}

		}
    }
	else
	{
		#pragma omp parallel for num_threads(local_threads) private(dr,dx,dy,dz,my_rand)
		for (long iWater = 0; iWater < number_of_waters; iWater++)
		{

			water_coords[iWater].x += my_rand.GetNormalRandomSTD(0.0f,random_sigma);
			water_coords[iWater].y += my_rand.GetNormalRandomSTD(0.0f,random_sigma);
			water_coords[iWater].z += my_rand.GetNormalRandomSTD(0.0f,random_sigma);

			// TODO 2x check that the periodic shifts are doing what they should be.
			// Check boundaries
			if (water_coords[iWater].x < size_neighborhood + 1)
			{
				water_coords[iWater].x = water_coords[iWater].x - 1*size_neighborhood +vol_nX;
			}
			else if (water_coords[iWater].x > vol_nX - size_neighborhood)
			{
				water_coords[iWater].x = water_coords[iWater].x - vol_nX + 1*size_neighborhood;
			}

			// Check boundaries
			if (water_coords[iWater].y < size_neighborhood + 1)
			{
				water_coords[iWater].y = water_coords[iWater].y - 1*size_neighborhood +vol_nY;
			}
			else if (water_coords[iWater].y > vol_nY - size_neighborhood)
			{
				water_coords[iWater].y = water_coords[iWater].y - vol_nY + 1*size_neighborhood;
			}

			// Check boundaries
			if (water_coords[iWater].z < size_neighborhood + 1)
			{
				water_coords[iWater].z = water_coords[iWater].z - 1*size_neighborhood +vol_nZ;
			}
			else if (water_coords[iWater].z > vol_nZ - size_neighborhood)
			{
				water_coords[iWater].z = water_coords[iWater].z - vol_nZ + 1*size_neighborhood;
			}
		}

	}

}


void Water::ReturnPadding(float max_tilt, float in_plane_rotation, int current_thickness, int current_nX, int current_nY, int* padX, int* padY, int* padZ)
{
	// Assuming tilting only along the Y-axis
	// TODO consider rotations of the projection which will also bring new water into view

	MyAssertTrue(max_tilt < 70.01, "maximum tilt angle supported is 70 degrees")

	float max_ip_ang = 45.0f;


	if( in_plane_rotation > max_ip_ang + .01)
	{
		wxPrintf("\n\n\t\tWarning, you have requested a tilt-axis rotation of %3.3f degrees, which is greater than the recommended max of %2.2f\n\t\tthis will add a lot of waters\n\n", in_plane_rotation, max_ip_ang);
	}

    // Now tack on the rotation padding
    int rot_padding =
    *padX = myroundint(0.5f*(float)current_nY * fabsf(sinf(in_plane_rotation * PIf / 180.0f)));;
    *padY = myroundint(0.5f*(float)current_nX * fabsf(sinf(in_plane_rotation * PIf / 180.0f)));;



    if (fabsf(max_tilt) < 1e-1)
    {
    	*padZ = 0;
    }
    else
    {

        float x0 = 0.5f*((float)current_nX + (float)*padX);
        float y0 = 0.0f;
        float z0 = -(float)current_thickness/2.0f;
        float xf,yf,zf;
        RotationMatrix rotmat;
        rotmat.SetToEulerRotation(0.0f,max_tilt,0.0f);
        rotmat.RotateCoords(x0, y0, z0, xf, yf, zf);

        *padZ = myroundint(fabsf(2.0f*(xf-x0)));
//    	*padZ = myroundint(1.0f*current_thickness*sinf(max_tilt * (float)PIf / 180.0f));
    }






}




