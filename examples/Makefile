all:
	$(CC) -Wall -o rmw_example rmw_example.c -pthread -lm
	$(CC) -Wall -o rmw_example_aba rmw_example_aba.c -pthread -lm -mcx16
	$(CC) -Wall -o simple_aba_example simple_aba_example.c -pthread
clean:
	rm -f rmw_example rmw_example_aba simple_aba_example 
check: all
	./rmw_example
	./rmw_example_aba
	./simple_aba_example 
