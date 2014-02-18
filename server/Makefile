server: server.cpp dl.cpp phy.cpp phy_setup.cpp header.h
	rm -f server 
	g++ server.cpp dl.cpp header.h phy.cpp phy_setup.cpp -lpthread -o server

clean:
	rm -f server
