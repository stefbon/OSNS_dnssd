bin_PROGRAMS =
sbin_PROGRAMS =

CC = gcc
RM = rm -f

extra_CFLAGS    = -fexceptions -fstack-clash-protection -arch x86_64 -Wall -Wextra -Wconversion -Wformat-security 
debug_CFLAGS    = -ggdb -Og
TARGET          =

osns_dnssd_CFLAGS = -D_FILE_OFFSET_BITS=64 -lz -lpthread -fPIC -lm -lsystemd -I . -losns -I include -I ./ssh -I /usr/include/osns-1 -Wall -Wno-unused-but-set-variable -Wno-uninitialized -Wno-unused-variable -Wno-unused-label -Wno-unused-function
osns_dnssd_LDFLAGS = -L/usr/lib64/libosns.so

osns_dnssd_SOURCES = main.c action/dnssd.c action/event.c action/pidfile.c

# osns_dnssd_OBJECTS = $(osns_dnssd_SOURCES:.c=.o)

# osns_dnssd : $(osns_dnssd_OBJECTS)
# 			$(CC) $(osns_dnssd_CFLAGS) $(osns_dnssd_LDFLAGS) -o osnshlpr_ssh.so $(osns_dnssd_OBJECTS)
# %.o : %.c $(osns_dnssd_SOURCES)
#			$(CC) $(osns_dnssd_CFLAGS) -c $< -o $@


sbin_PROGRAMS += osns_dnssd

.PHONY: clean

# clean:
# 			-$(RM) $(osns_dnssd_OBJECTS)


#all: $(TARGET)

