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


server_objects = physical_layer.o datalink_layer.o
server_source = physical_layer.cpp datalink_layer.cpp server_appLayer.cpp client_appLayer.cpp server_main.cpp

client_source = physical_layer.cpp datalink_layer.cpp client_appLayer.cpp server_appLayer.cpp client_main.cpp

#// make
all: server client

server: $(server_source)
	g++ $(server_source) -o server -lpthread -lrt

client: $(client_source)
	g++ $(client_source) -o client -lpthread -lrt

clean:
	rm -f *.o
	rm -f testProg
