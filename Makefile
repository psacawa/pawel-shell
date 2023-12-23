all: pawel-shell

pawel-shell: shell.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm pawel-shell

.PHONY: clean
