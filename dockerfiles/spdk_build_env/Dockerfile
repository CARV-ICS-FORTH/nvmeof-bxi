FROM rockylinux:9

ENV LC_ALL=C.UTF-8 LANG=C.UTF-8

RUN dnf -y update && \
    dnf -y install dnf-plugins-core epel-release && \
    dnf config-manager --set-enabled crb && \
    dnf -y install \
      git gcc gcc-c++ make cmake pkgconfig \
      python3 python3-pip \
      numactl numactl-devel \
      libuuid-devel libaio-devel openssl-devel \
      libibverbs libibverbs-devel rdma-core-devel \
      elfutils-libelf-devel libcap-ng-devel \
      fuse3 fuse3-devel \
      libasan \
      ncurses-devel \
      which file tar xz bzip2 gzip wget ca-certificates \
      vim less iproute iputils ethtool \
      diffutils procps-ng \
      autoconf automake libtool \
      clang clang-analyzer \
      bear \
    && dnf clean all

# Additional packages required for kernel building
RUN dnf -y install \
      bc \
      bison \
      flex \
      dwarves \
      rpm-build \
      perl \
      perl-interpreter \
      perl-devel \
      kmod \
    && dnf clean all

# Build and install CUnit from source
RUN cd /tmp && \
    wget https://sourceforge.net/projects/cunit/files/CUnit/2.1-3/CUnit-2.1-3.tar.bz2 && \
    tar xjf CUnit-2.1-3.tar.bz2 && \
    cd CUnit-2.1-3 && \
    libtoolize && aclocal && autoheader && autoconf && automake --add-missing && \
    ./configure --prefix=/usr && \
    make && make install && \
    cd / && rm -rf /tmp/CUnit-*

# Tools for RPM-based kernel builds
RUN dnf -y install \
      rpmdevtools \
      mock \
    && dnf clean all


# Python tooling for SPDK
# Added 'scan-build' to provide the 'intercept-build' command
RUN python3 -m pip install --no-cache-dir --upgrade pip && \
    python3 -m pip install --no-cache-dir \
      meson==0.63.* \
      ninja==1.10.* \
      pyelftools \
      scan-build

# BXI / Bull-specific headers and libraries
COPY portals4.h /usr/include/
COPY portals4_bxiext.h /usr/include/
RUN mkdir -p /usr/include/linux/bxi3
COPY bxi3/ /usr/include/linux/bxi3/

COPY libportals-bxi3.so /lib64/
RUN ln -s /lib64/libportals-bxi3.so /lib64/libportals.so

RUN ldconfig

WORKDIR /work
CMD ["/bin/bash"]
