

libfiles  = clock_handler 
libfiles += data_transport 
libfiles += device_reader
libfiles += device_repository 
libfiles += event_handlers 
libfiles += ip_reader
libfiles += lldp_monitor 
libfiles += lldp_packet 
libfiles += lldp_repository
libfiles += netlink_monitor 
libfiles += owned_file_descriptor

object_files = $(addprefix ../out/,$(addsuffix .o, $(libfiles)))
input_files = $(addprefix src/,$(addsuffix .cc, $(libfiles)))

test_files  = inplace_vector

test_executables = $(addprefix ../test_executables/test_, $(test_files))

WARNINGS = -Wall -Wextra -Werror 

GCC = g++ -g -std=c++23 $(WARNINGS) -I./include

all: ../out/ndisc ../out/ndisc_client $(test_executables)

../out/ndisc: $(object_files) include/*.hh
	$(GCC) $(object_files) src/server_main.cc -o ../out/ndisc

../out/ndisc_client: $(object_files) src/client_main.cc include/*.hh
	$(GCC) $(object_files) src/client_main.cc -o ../out/ndisc_client


$(object_files): ../out/%.o : src/%.cc include/*.hh
	$(GCC) -c $< -o $@

$(test_executables): ../test_executables/% : test/%.cc include/*.hh $(object_files)
	$(GCC) $(object_files) $< -o $@

clean:
	rm -rf ../out

test_all: $(test_executables)
	$(foreach exe, $(test_executables), $(exe); )

.PHONY: clean all test_all