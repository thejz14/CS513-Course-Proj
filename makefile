#*************************************
#* [CS513] Group Project
#* makefile written by
#* Hanxiong Shi - shihanxiong@wpi.edu
#* Evan Frenn - ejfrenn@wpi.edu
#*************************************


#// This makefile is used as follows to regenerate files.
#// make FTP_Server		--regenerates the executable FTP_Server program
#// make FTP_Client		--regenerates the executable FTP_Client program
#// make			--same as "make FTP_Server" & "make FTP_Client"
#// make clean			--clean all temporary *~ / *.o files and executable FTP_Server/FTP_Client


#// make
all: physical_layer.o datalink_layer.o

EnhancedEchoServer:     ejf_EnhancedEchoServer.o

EnhancedEchoClient:     ejf_EnhancedEchoClient.o

ejf_EnhancedEchoServer.o: ejf_EnhancedEchoServer.cpp
        g++ ejf_EnhancedEchoServer.cpp -o ejf_EnhancedEchoServer

ejf_EnhancedEchoClient.o: ejf_EnhancedEchoClient.cpp
        g++ ejf_EnhancedEchoClient.cpp -o ejf_EnhancedEchoClient

clean:
      	rm -f *.o

