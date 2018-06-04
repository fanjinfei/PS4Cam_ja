test: main.cpp ps4eye.cpp
	g++ -g -std=c++11 -o test main.cpp ps4eye.cpp -I./ -lX11 -lpthread -lusb-1.0 -ljpeg

read_raw: read_raw.cpp
	g++ -g -std=c++11 -o read_raw read_raw.cpp movie.cpp  -ljpeg -lx264 -lswscale -lavutil -lavformat -lavcodec -lcairo
clean:
	rm -rf test *.o
