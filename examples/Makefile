all:
	$(CC) -Wall -o rmw_example rmw_example.c -pthread -lm
	$(CC) -Wall -o rmw_example_aba rmw_example_aba.c -pthread -lm -mcx16
clean:
	rm -f rmw_example rmw_example_aba
check: all
	./rmw_example
	./rmw_example_aba
