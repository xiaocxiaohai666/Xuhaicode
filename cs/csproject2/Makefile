all:service client

service:service.cpp
	g++ -g -O2 -o service service.cpp -lpthread -lmysqlclient -ljsoncpp

client:client.cpp
	g++ -g -O2 -o client client.cpp -lpthread -lmysqlclient -ljsoncpp

clean:
	rm -f *.o service client
