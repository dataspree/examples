# Build and run:
#   docker build -t clion/ubuntu/cpp-env:1.0 -f Dockerfile.cpp-env-ubuntu .

FROM ubuntu:22.04

ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="3.25.0"
ENV DEBIAN_FRONTEND=noninteractive

# Dataspree network hack. Avoids timeout.
RUN apt-get update && apt-get -y dist-upgrade
RUN apt-get -y install build-essential clang make cmake autoconf automake libtool valgrind locales-all dos2unix rsync tar openssh-server gdb lsb-release wget software-properties-common gnupg curl ccache  libgtk2.0-dev


# Install CMake
COPY ./reinstall-cmake.sh /tmp/
RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
        chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
    fi \
    && rm -f /tmp/reinstall-cmake.sh
RUN cmake --version \
    && which cmake

# Install clang++
RUN bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"
RUN apt-get update \
    && apt-get -y install --no-install-recommends python3-pip
RUN apt-get -y install clang-tools-16 clang-16

# Configure SSH
RUN /usr/bin/echo -e 'LogLevel DEBUG2\nPermitRootLogin yes\nPasswordAuthentication yes\nPasswordAuthentication yes\nSubsystem sftp /usr/lib/openssh/sftp-server' > /etc/ssh/sshd_config 
RUN mkdir /run/sshd
RUN ssh-keygen -A && service ssh --full-restart

#RUN echo "export PATH=$PATH:/root/.local/bin" >> /root/.bashrc \
    #echo "export CC=clang CXX=clang++" >> /root/.bashrc
#ENV PATH "$PATH:/root/.local/bin"



RUN groupadd -r dataspree && useradd -g dataspree dataspree
ENV HOME /root
ENV CC /usr/bin/clang-16
ENV CXX /usr/bin/clang++-16
RUN chown -R dataspree:dataspree /root
ENV CONAN_USER_HOME=/root/
ENV PATH "$PATH:/root/.local/bin"
COPY --chown=dataspree:dataspree ./conan-profile /tmp/root/.conan/profiles/default

RUN which clang && which clang++
RUN ls /usr/bin/clang
RUN rm /usr/bin/clang
RUN rm /usr/bin/clang++
RUN ln -s /usr/bin/clang++-16 /usr/bin/clang++
RUN ln -s /usr/bin/clang-16 /usr/bin/clang

RUN apt install -y libstdc++-12-dev

USER dataspree
RUN python3 -m pip install --user conan==1.56.0
RUN mkdir -p /root/.conan/data
RUN ls -ltra /root/.conan/data



#USER root
#CMD /usr/sbin/sshd -D -e -f /etc/ssh/sshd_config

#CMD tail -f /dev/null
