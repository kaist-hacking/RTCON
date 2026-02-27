# name of your application
APPLICATION = riot-fuzz

ifndef RIOT_ROOT
$(error RIOT_ROOT is not set)
endif

TOOLCHAIN = clang
TOOLCHAIN_SURFFIX = -cgcc
# CFLAGS += -DTHREAD_STACKSIZE_MAIN=1073741824
CFLAGS += -DTHREAD_STACKSIZE_MAIN=2097152

WERROR = 0

CFLAGS += -fno-pie -no-pie
CFLAGS += -DNATIVE_MEMORY -DCRASH_DIR_PATH=\"${CRASH_DIR_PATH}\"
CFLAGS += -DFUZZ_COVERAGE -fprofile-instr-generate -fcoverage-mapping
CFLAGS += -DCOVERAGE_DIR_PATH=\"${COVERAGE_DIR_PATH}\"
ifdef LOW_FUZZ
CFLAGS += -DLOW_FUZZ
endif
CXXFLAGS += -DNATIVE_MEMORY
LINKFLAGS += -fsanitize=address,fuzzer -fno-omit-frame-pointer -fsanitize-recover=address -rdynamic
LINKFLAGS += -fprofile-instr-generate -fcoverage-mapping
LINKFLAGS += -fno-pie -no-pie

# If no BOARD is found in the environment, use this default:
BOARD ?= native64

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(RIOT_ROOT)

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

USEMODULE += fmt
USEMODULE += od_string
USEMODULE += ipv4
USEMODULE += ipv6
USEMODULE += ipv6_addr
USEMODULE += dhcpv6
USEMODULE += iolist
USEMODULE += sock
USEMODULE += sock_ip
USEMODULE += sock_tcp
USEMODULE += sock_udp
USEMODULE += sock_util
USEMODULE += ztimer_usec
USEMODULE += prng_minstd
USEMODULE += netif

# GNRC
ifneq ($(LWIP_FUZZ), true)
USEMODELE += gnrc_sock
USEMODULE += gnrc_sock_ip
USEMODULE += gnrc_sock_tcp
USEMODULE += gnrc_sock_udp
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_dhcpv6_client
USEMODULE += dhcpv6_client
endif

ifeq ($(LORAWAN_FUZZ), true)
USEMODULE += gnrc_lorawan
endif

ifeq ($(NIMBLE_FUZZ), true)
FEATURES_PROVIDED += ble_nimble
FEATURES_PROVIDED += ble_adv_ext
FEATURES_PROVIDED += ble_nimble_netif

# glue code
USEMODULE += nimble_riot_contrib

# RIOT port
USEMODULE += nimble_porting_nimble

# Generic modules used by host and controller
USEMODULE += nimble_transport

USEMODULE += nimble_host
USEMODULE += nimble_host_util
USEMODULE += nimble_host_store_ram

USEMODULE += nimble_netif
USEMODULE += nimble_adv_ext
USEMODULE += nimble_autoconn_ipsp

USEMODULE += nimble_rpble
USEMODULE += nimble_scanlist
endif

ifeq ($(LWIP_FUZZ), true)
USEPKG += lwip
USEMODULE += lwip_core
USEMODULE += lwip_ipv4
USEMODULE += lwip_ipv6
USEMODULE += lwip_sock_ip
USEMODULE += lwip_contrib
USEMODULE += lwip_sock_tcp
USEMODULE += lwip_sock_udp
USEMODULE += lwip_netif
USEMODULE += lwip_netif_init_devs
endif

FEATURES_OPTIONAL += picolibc

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
