

libfiles = lldp_packet netlink_monitor lldp_monitor data_transport
object_files = $(addprefix ../out/,$(addsuffix .o, $(libfiles)))
input_files = $(addprefix src/,$(addsuffix .cc, $(libfiles)))

WARNINGS = -Wall -Wextra -Werror 

GCC = g++ -g -std=c++23 $(WARNINGS) -I./include

all: ../out/ndisc ../out/ndisc_client

../out/ndisc: $(object_files) include/*.hh
	$(GCC) $(object_files) src/server_main.cc -o ../out/ndisc

../out/ndisc_client: $(object_files) include/*.hh
	$(GCC) $(object_files) src/client_main.cc -o ../out/ndisc_client


$(object_files): ../out/%.o : src/%.cc include/*.hh
	$(GCC) -c $< -o $@

.PHONY: clean all
clean:
	rm -rf ../out