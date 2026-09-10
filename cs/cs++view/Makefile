all:service client

service:service.cpp service.h
	g++ -std=c++11 -g -O0 -o service service.cpp -lpthread -lmysqlclient -ljsoncpp

client:client.cpp client.h
	g++ -std=c++11 -g -O0 -o client client.cpp -lpthread -lmysqlclient -ljsoncpp

clean:
	rm -f *.o service client
