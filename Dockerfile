FROM ubuntu:20.04
RUN apt-get update -y
RUN apt-get install wget build-essential -y
WORKDIR /opt/
RUN wget https://github.com/DigiByte-Core/digibyte/releases/download/v8.22.1/digibyte-v8.22.1-x86_64-linux-gnu.tar.gz
RUN tar zxvf digibyte-v8.22.1-x86_64-linux-gnu.tar.gz
RUN mv digibyte-664c6a372bd2/bin/digibyted /usr/bin
RUN mv digibyte-664c6a372bd2/bin/digibyte-cli /usr/bin
RUN rm -R digibyte-664c6a372bd2
RUN wget https://raw.githubusercontent.com/TheRetroMike/rmt-nomp/master/scripts/blocknotify.c
RUN gcc blocknotify.c -o /usr/bin/blocknotify
CMD /usr/bin/digibyted -printtoconsole
