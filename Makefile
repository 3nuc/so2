compile: bigbuf.c
	gcc -DMODULE -D__KERNEL__ -O2 -c bigbuf.c -o bigbuf.o
clean:
	rm -rf *.o
