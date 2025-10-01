default: prod cons

all: clean prod cons

prod: producer.c shared.h
	gcc producer.c -o prod -lrt

cons: consumer.c shared.h
	gcc consumer.c -o cons -lrt

clean:
	rm -f prod cons *~
