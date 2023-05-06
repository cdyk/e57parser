E57PARSER_SRC_DIR = ../src
CCFLAGS  += -Wall -O2
CXXFLAGS += -Wall -O2 -std=c++20
OBJDIR = obj

E57PARSER_CXX_SRC = $(wildcard $(E57PARSER_SRC_DIR)/*.cpp)
E57PARSER_CXX_OBJ = $(patsubst $(E57PARSER_SRC_DIR)/%.cpp, $(OBJDIR)/%.o, $(E57PARSER_CXX_SRC))


E57PARSER_C_SRC = $(wildcard $(E57PARSER_SRC_DIR)/*.c)
E57PARSER_C_OBJ = $(patsubst $(E57PARSER_SRC_DIR)/%.c, $(OBJDIR)/%.o, $(E57PARSER_C_SRC))

.PHONY: all objdir clean

all: objdir e57parser

e57parser: $(E57PARSER_CXX_OBJ) $(E57PARSER_C_OBJ)
	$(CXX)  $(LDFLAGS) -o $@ $^

$(E57PARSER_CXX_OBJ): $(OBJDIR)/%.o : $(E57PARSER_SRC_DIR)/%.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(E57PARSER_C_OBJ): $(OBJDIR)/%.o : $(E57PARSER_SRC_DIR)/%.c
	$(CC) -c $(CCFLAGS) $< -o $@

objdir:
	@mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) e57parser
