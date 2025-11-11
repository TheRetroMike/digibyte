#FROM ubuntu:20.04
#RUN apt-get update -y
#RUN apt-get install wget build-essential -y
#WORKDIR /opt/
#RUN wget https://github.com/DigiByte-Core/digibyte/releases/download/v8.22.1/digibyte-v8.22.1-x86_64-linux-gnu.tar.gz
#RUN tar zxvf digibyte-v8.22.1-x86_64-linux-gnu.tar.gz
#RUN mv digibyte-664c6a372bd2/bin/digibyted /usr/bin
#RUN mv digibyte-664c6a372bd2/bin/digibyte-cli /usr/bin
#RUN rm -R digibyte-664c6a372bd2
#RUN wget https://raw.githubusercontent.com/TheRetroMike/rmt-nomp/master/scripts/blocknotify.c
#RUN gcc blocknotify.c -o /usr/bin/blocknotify
#CMD /usr/bin/digibyted -printtoconsole

FROM ubuntu:22.04
RUN apt-get update -y
RUN apt-get install build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 make automake cmake curl libtool binutils-gold bsdmainutils pkg-config python3 patch bison -y
WORKDIR /app
COPY . .
WORKDIR /app/depends
RUN make -j $(( $(nproc) - 1 ))
RUN make install
WORKDIR /app
RUN ./autogen.sh
RUN CONFIG_SITE=/app/depends/x86_64-pc-linux-gnu/share/config.site ./configure
RUN make -j $(( $(nproc) - 1 ))
RUN mv /app/src/digibyted /app/digibyted
RUN mv /app/src/digibyte-cli /app/digibyte-cli
CMD /app/digibyted -printtoconsole
