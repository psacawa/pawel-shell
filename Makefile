all: pawel-shell

pawel-shell: shell.c
	$(CC) $(CFLAGS) $< -o $@ $(shell pkg-config --libs readline)

clean:
	rm pawel-shell

.PHONY: clean
