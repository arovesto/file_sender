# file_sender
This is my 1c entering project. It is can send file via socket interface, with usage of multitreading

# usage
compile under your favorite compiler and run with command:
​ program -recieve
​ program -send -ip <IP> -file <FILENAME> -threads <THREAD>

FILENAME is the name of file,
IP is ip of computer to send file,
THREAD is ammounts of threads to use (only one on Linux systems, sorry)

# how does it works
Receive rutine binds to concrete socket, and waits for sending routine
then it gets filename and threads ammount and starts to get data via threads
input files is split into THREAD ammount of almost equal parts and sended via socket.
And receive routine creates empty file, and adds all data in correct places.
File is never stored in memory completely, so it could be big enough.
