build:
	mpic++ -o bitorrent bitorrent.cpp -std=c++17 -pthread -Wall

clean:
	rm -rf bitorent
