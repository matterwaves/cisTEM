# syntax=docker/dockerfile:1
FROM bhimesbhimes/cistem_build_env:base_image_v2.0.2

# To allow a variety of common options, arguments may be passed to the build_container script, 
# which have the side-effect that this top-level layer is rebuilt without cache every time.
#
# Generally, the thinking is that all top-level additions are relatively cheap to build, although this may
# not be quite true for WX.

# Options are still somewhat restricted
#   build-type = linking is static or dynamic
#   build-compiler = icpc or g++
#   build-wx-version = old (3.0.5) or new (3.1.5) NOTE: we probably want to test 3.2 instead
#   build-npm = false [ default ] or true if specified
#   build-ref-images = false [ default ] or true if specified
ARG n_threads=12
ARG DEBIAN_FRONTEND=noninteractive
ARG TZ=America/New_York
ARG GCC_VER=11
# note "-" in the variable seems to break the conditional statements.
ARG build_type="static"
ARG build_compiler="icpc"
ARG build_wx_version="stable"
ARG build_npm="false"
ARG build_ref_images="false"

SHELL ["/bin/bash", "-c"]
# some rebuild comment
ENV CISTEM_REF_IMAGES /cisTEMdev/cistem_reference_images
ENV LD_RUN_PATH=/opt/WX/intel-dynamic/lib:${LD_RUN_PATH}

# Install wxWidgets
COPY install_wx_3.0.5_static.sh /tmp/install_wx_3.0.5_static.sh
COPY install_wx_3.0.5_dynamic.sh /tmp/install_wx_3.0.5_dynamic.sh
COPY install_wx_3.1.5.sh /tmp/install_wx_3.1.5.sh
COPY install_node_16.sh /tmp/install_node_16.sh

# NOTE: if working on setting up dynamic linking, we will need to add a step to install wxWidgets. The script is already at top_image/install_wx_3.0.5_dynamic.sh
RUN echo "install wxWidgets 3.05" && /tmp/install_wx_3.0.5_static.sh && /tmp/install_wx_3.0.5_dynamic.sh
RUN 

# If we do not do this, we want to link the static or dynamic libs from /opt/WX to /usr/bin so we don't need to set it on configure lines and also so that wxformbuilder can find them
RUN echo "checking for wxWidgets 3.1.5" && if [[ "x${build_wx_version}" == "xdev" ]]; then /tmp/install_wx_3.1.5.sh  ; else echo "linking the (${build_type}) wx-config system wide"  && ln -sf /opt/WX/intel-${build_type}/bin/wx-config /usr/bin/wx-config ; fi

# Get reference images for testing and debugging
RUN if [[ "x${build_ref_images}" == "true" ]]; then mkdir -p /cisTEMdev && pip3 install gdown && cd /cisTEMdev && gdown https://drive.google.com/drive/folders/1PO9tBU7lKqKUuSb8qdEPvlEk7xeektXv?usp --folder ; fi


# Will this work with wx 3.0.5?

# Install wxFormbuilder
# Note, this will ignore the stable dynamic wx at /opt/WX/intel-dynamic and install libwxbase3.0-0v5 and libwxgtk3.0-gtk3-0v5 and libwxgtk-media3.0-gtk3-0v5
RUN cd /tmp && wget https://github.com/wxFormBuilder/wxFormBuilder/releases/download/v3.10.0/wxformbuilder_3.10.0_ubuntu-20.04_amd64.deb && \
    apt-get update && apt install -y ./wxformbuilder_3.10.0_ubuntu-20.04_amd64.deb && rm wxformbuilder_3.10.0_ubuntu-20.04_amd64.deb && \
    rm -rf /var/lib/apt/lists/*

# # Install Node 16
RUN echo "build npm" && if [[ "x${build_npm}" == "xtrue" ]] ; then /tmp/install_node_16.sh ; fi

# TODO: if adding dynamic linking for the cuda libs, we can save a bunch of space in the image by removing these large static libs
# Use the basename bit to ensure no rm -rf foibles with root dir in empyt string case
# RUN ls /usr/local/cuda/lib64/lib*_static.a | grep -v cufft_static.a | while read a; do rm -rf /usr/local/cuda/lib64/$(basename $a); done && \
#     rm -rf /usr/local/cuda/lib64/libcufft_static_nocallback.a



USER cisTEMdev
WORKDIR /home/cisTEMdev

