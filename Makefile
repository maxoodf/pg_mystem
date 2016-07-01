EXTENSION = pg_mystem        # the extensions name
DATA = pg_mystem--1.0.1.sql  # script files to install
MODULES = pg_mystem

# postgres build stuff
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
SHARE_FOLDER := $(shell $(PG_CONFIG) --sharedir)

CXXFLAGS = -fPIC -std=c++11 -DSHARE_FOLDER="$(SHARE_FOLDER)"

include $(PGXS)
