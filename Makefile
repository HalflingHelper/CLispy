prompt: main.c mathutil.c
	$(CC) -std=c99 -Wall main.c mathutil.c mpc.c -ledit -lm -o lispy