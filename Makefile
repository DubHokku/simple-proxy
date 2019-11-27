TARGET = ast
SETPATH = /usr/local/bin
# g++ -std=c++11 -lnuma -fopenmp -o ast ast.cc
.PHONY: all clean install uninstall

all: $(TARGET)

# -- make obj
# g++ -c -std=c++11 -L/usr/local/lib -lcurl -o server.o recipient.cc
server.o: recipient.cc
	g++ -c -std=c++11 -o server.o recipient.cc


main.o: $(TARGET).cc
	g++ -c -std=c++11 -o main.o $(TARGET).cc


# -- make bin
# g++ -o $(TARGET) -lpthread -L/usr/local/lib -lcurl main.o server.o
$(TARGET): server.o main.o
	g++ -o $(TARGET) main.o server.o


# -- make install
clean:
	rm -rf $(TARGET) main.o server.o client.o

install:
	install $(TARGET) $(SETPATH)

uninstall:
	rm -rf $(SETPATH)/$(TARGET)
