client: client.cpp dl.cpp phy.cpp phy_setup.cpp header.h
	rm -f chat_history.txt
	rm -f client
	g++ client.cpp dl.cpp header.h phy.cpp phy_setup.cpp  -lpthread -o client 

clean:
	rm -f chat_history.txt
	rm -f client

