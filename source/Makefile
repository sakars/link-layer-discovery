

libfiles = client 
libfiles += clock_handler 
libfiles += device_reader
libfiles += device_repository 
libfiles += event_handlers 
libfiles += ip_reader
libfiles += lldp_monitor 
libfiles += lldp_packet 
libfiles += lldp_repository
libfiles += lldp_sender
libfiles += netlink_monitor 
libfiles += owned_file_descriptor

object_files = $(addprefix ../out/,$(addsuffix .o, $(libfiles)))
input_files = $(addprefix src/,$(addsuffix .cc, $(libfiles)))

WARNINGS = -Wall -Wextra -Werror 
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=126617
WARNINGS += -Wno-error=stringop-overread


GCC = g++ -g -O2 -std=c++23 $(WARNINGS) -I./include

all: ../out/ndisc ../out/ndisc_client

../out/ndisc: $(object_files) src/server_main.cc include/*.hh
	$(GCC) $(object_files) src/server_main.cc -o ../out/ndisc

../out/ndisc_client: $(object_files) src/client_main.cc include/*.hh
	$(GCC) $(object_files) src/client_main.cc -o ../out/ndisc_client


$(object_files): ../out/%.o : src/%.cc include/*.hh
	$(GCC) -c $< -o $@

clean:
	rm -rf ../out/*

.PHONY: clean all test_all