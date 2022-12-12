server: server.c udp.c
	gcc -o server server.c udp.c -Wall
client: client.c udp.c mfs.c
	gcc -o client client.c udp.c mfs.c -Wall
clean:
	rm -f client server mkfs *.img
mkfs: mkfs.c
	gcc -o mkfs mkfs.c
