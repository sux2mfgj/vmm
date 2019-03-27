
test: test.c
	gcc $< -o $@

.PHONY: run
run: test
	./$<

.PHONY: clean
clean:
	rm -rf test
