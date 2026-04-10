# Inputs
NUMP ?= 1
NUMQ ?= 1

default:
	g++ -O3 -march=native frontend.cpp -o frontend `pkg-config --cflags --libs libdpdk`

run:
	@python3 run.py $(NUMP) $(NUMQ)

clean:
	@rm -f backend frontend
