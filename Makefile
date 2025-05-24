###############################################################################
# Variables
###############################################################################
CC          = gcc
CFLAGS      = $(shell pkg-config --cflags igraph glib-2.0) -I/usr/include/igraph -I/usr/include/x86_64-linux-gnu
LDLIBS      = $(shell pkg-config --libs igraph glib-2.0)
INSTALL_DIR = /usr/local/bin

# Executables to build
BINARIES    = compare_jdm random_jdm ibrido

###############################################################################
# Phony Targets
###############################################################################
.PHONY: all clean install uninstall help debug dist

###############################################################################
# Default Target
###############################################################################
all: $(BINARIES)

###############################################################################
# Build compare_jdm
###############################################################################
compare_jdm: compare_jdm.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

###############################################################################
# Build random_jdm
###############################################################################
random_jdm: random_jdm.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

###############################################################################
# Build ibrido (ex joint_model_ottimizzato)
###############################################################################
ibrido: ibrido.c
	$(CC) -O3 -o $@ $^ $(CFLAGS) $(LDLIBS) -lm

###############################################################################
# Debug build (re-build everything with debug flags)
###############################################################################
debug: CFLAGS += -g -O0
debug: clean all

###############################################################################
# Install: copy binaries to a system directory
###############################################################################
install: all
	@echo "Installing to $(INSTALL_DIR)..."
	mkdir -p "$(INSTALL_DIR)"
	@for bin in $(BINARIES); do \
		echo "  Installing $$bin"; \
		install $$bin "$(INSTALL_DIR)"; \
	done

###############################################################################
# Uninstall: remove binaries from system directory
###############################################################################
uninstall:
	@echo "Removing from $(INSTALL_DIR)..."
	@for bin in $(BINARIES); do \
		if [ -f "$(INSTALL_DIR)/$$bin" ]; then \
			echo "  Removing $$bin"; \
			rm -f "$(INSTALL_DIR)/$$bin"; \
		fi; \
	done

###############################################################################
# Dist: create a source tarball named 2k_simple.tar.gz
###############################################################################
dist:
	@echo "Creating distribution archive..."
	mkdir -p dist
	@cp *.c *.h Makefile dist 2>/dev/null || true
	tar -czf 2k_simple.tar.gz dist
	rm -rf dist
	@echo "Created 2k_simple.tar.gz"

###############################################################################
# Clean
###############################################################################
clean:
	rm -f $(BINARIES)
	rm -rf dist 2k_simple.tar.gz

###############################################################################
# Help: List available targets
###############################################################################
help:
	@echo "Makefile targets:"
	@echo "  all        - Build all binaries (default)"
	@echo "  debug      - Build all binaries with debug flags"
	@echo "  install    - Install binaries to $(INSTALL_DIR)"
	@echo "  uninstall  - Remove binaries from $(INSTALL_DIR)"
	@echo "  dist       - Create a tarball (2k_simple.tar.gz)"
	@echo "  clean      - Remove build artifacts"
	@echo "  help       - Show this help message"
