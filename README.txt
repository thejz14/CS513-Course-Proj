/*************************************
 * [CS513] Project
 * README written by
 * Evan Frenn - ejfrenn@wpi.edu
 *************************************/

=COMPILATION INSTRUCTION=
	Simply run make. There will be two outputs ejf_server and ejf_client.

=START COMMANDS=
	ejf_client [server IP/hostname]
	ejf_client
=START OPTIONS=
	-E=x	where x is the error rate from 0 to 100
	-L	prints out the data link layer info
	-W=x	where x is the window size desired to use(default is 4)

=COMMANDS(working)=
	login [username] [password] - logs in the user. You must be logged in to run any other commands. There is a dummy user set up as user123 with password abc123. usernames and passwords
					are stored in .userDB

	download start [filename] - begins to transfer a file from the client's repo on the server to the location of the client executable. There is a test file for user123 of skiTest.jpg

	download pause - pauses the current file transfer

	download resume [filename] - starts to resume a paused download

	downlaod cancel [filename] - cancels either a current download or a paused download

	logout - logs out the user. On the server, this will right all statistics to a file named [username]-stats

	stats - prints out the current statistics of the communication with the server

=COMMANDS(not working)
	
	download list - this command is not currently implemented


=OVERALL STRUCTURE=
	The different layers of the applications each have their own associated source file. The layers are implemented with threads and mutexes for concurrency each layer talks to the lower layer
by calling the lower layer send/recv functions (EX data link layer interacts with physical layer by calling ph_send() and ph_recv(). Timeouts are performed using signals in the data link layer.
There is currently no way to exit either application without using ctrl c. Everything for the physical and data link layer are working correcly to the best of my knowledge. The app layer list command
is not implemented

=CLIENT APP STRUCTURE=
	The client application is structured into two threads. The main thread handles I/O with the user, while the background thread handles downloading a file/ receiving from the server. 
