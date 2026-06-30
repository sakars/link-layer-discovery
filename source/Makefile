

files = lldp_packet netlink_monitor main scheduler lldp_monitor
object_files = $(addprefix ../out/,$(addsuffix .o, $(files)))
input_files = $(addprefix src/,$(addsuffix .cc, $(files)))

WARNINGS = -Wall -Wextra -Werror 

GCC = g++ -g -std=c++20 $(WARNINGS) -I./include


../out/ndisc: ../out $(object_files)
	$(GCC) $(object_files) -o ../out/ndisc

../out:
	mkdir -p ../out

$(object_files): ../out/%.o : src/%.cc include/*.hh
	$(GCC) -c $< -o $@
