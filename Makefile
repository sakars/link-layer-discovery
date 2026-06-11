


out/ndisc: include/*.hh src/*.cc
	mkdir -p out
	g++ -g -Wall -Wextra -Werror -I./include src/* -o out/ndisc
