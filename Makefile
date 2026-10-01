CC ?= gcc
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

CFLAGS ?= -Wall -Wextra -pthread -I$(INC_DIR) -O2 -g -MMD -MP
LDFLAGS ?= -pthread

COMMON_OBJS = $(OBJ_DIR)/net_util.o

TARGETS = $(BIN_DIR)/server_unsafe \
          $(BIN_DIR)/server_safe \
          $(BIN_DIR)/server_save \
          $(BIN_DIR)/server_pool \
          $(BIN_DIR)/server_pool_sem \
          $(BIN_DIR)/load_client

.PHONY: all clean server_unsafe server_safe server_save server_pool server_pool_sem load_client

all: $(TARGETS)

# Creación de carpetas como prerrequisito de orden
$(OBJ_DIR) $(BIN_DIR):
	@mkdir -p $@

# Compilación de objetos C
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Enlazado de ejecutables
$(BIN_DIR)/server_unsafe: $(OBJ_DIR)/server_unsafe.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/server_safe: $(OBJ_DIR)/server_safe.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/server_save: $(OBJ_DIR)/server_save.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/server_pool: $(OBJ_DIR)/server_pool.o $(COMMON_OBJS) $(OBJ_DIR)/conn_queue.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/server_pool_sem: $(OBJ_DIR)/server_pool_sem.o $(COMMON_OBJS) $(OBJ_DIR)/conn_queue_sem.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/load_client: $(OBJ_DIR)/load_client.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Alias para compilar individualmente
server_unsafe: $(BIN_DIR)/server_unsafe
server_safe: $(BIN_DIR)/server_safe
server_save: $(BIN_DIR)/server_save
server_pool: $(BIN_DIR)/server_pool
server_pool_sem: $(BIN_DIR)/server_pool_sem
load_client: $(BIN_DIR)/load_client

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) *.dSYM

-include $(wildcard $(OBJ_DIR)/*.d)
