EXTENSION = pg_mystem        # the extensions name
DATA = pg_mystem--1.0.1.sql  # script files to install
MODULES = pg_mystem

# postgres build stuff
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
SHARE_FOLDER := $(shell $(PG_CONFIG) --sharedir)
DOC_LEN_MAX := 65536
MYSTEM_PROCS := 8

CXXFLAGS = -fPIC -fpic -std=c++11 -DSHARE_FOLDER="$(SHARE_FOLDER)" -DDOC_LEN_MAX=$(DOC_LEN_MAX) -DMYSTEM_PROCS=$(MYSTEM_PROCS)

include $(PGXS)
