all:
	g++ -o dchat -Wall -lpthread addrlist.cpp getip.cpp sequencer.cpp client.cpp main.cpp

test:
	g++ -o dchat -Wall -DTEST addrlist.cpp main.cpp

debug:
	g++ -o dchat -g sequencer.cpp main.cpp
clean:
	rm *.o
	rm dchat