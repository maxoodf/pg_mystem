pg_mystem - PostgreSQL extension for Yandex Mystem.
============

pg_mystem is an implementation of the PostgreSQL extension for Yandex Mystem (morphology analyzer/stemmer for Russian language).

Read more about PostgreSQL extensions here -  https://www.postgresql.org/docs/9.5/static/extend-extensions.html and here - https://www.postgresql.org/docs/9.5/static/extend-pgxs.html

Read more about Yandex Mystem - https://tech.yandex.ru/mystem/

What is the extension function? You can use the power of Mystem inside your PostgreSQL database.

Yandex Mystem Installation
============
You can download binary file from https://tech.yandex.ru/mystem/ and install it to the PostgreSQL share directory. 
For example:
```bash
$ wget http://download.cdn.yandex.net/mystem/mystem-3.0-linux3.1-64bit.tar.gz
$ tar xfz mystem-3.0-linux3.1-64bit.tar.gz
$ sudo cp ./mystem `pg_config --sharedir`
```

pg_mystem Installation
============
```bash
$ git clone https://github.com/maxoodf/pg_mystem.git
$ cd ./pg_mystem
$ make
$ sudo make install
```

pg_mystem Configuration
============
You may wish to change pg_mystem default settings. All need is to change Makefile defined parameters - 

1. `DOC_LEN_MAX` - maximal document (string) length. If you work with a short lines, redefine `DOC_LEN_MAX` to 1000 chars or so. If you work with a big documents, redefine `DOC_LEN_MAX` to 100000 or so.

2. `MYSTEM_PROCS` - how many Mystem processes to run. I use the following value in my projects - one Mystem process throughput is about 6 KB/sec. So if I need to process, say 30-35 KB of text in a second I use 6 Mystem processes.

You will need to reinstall pg_mystem in case any of these parameters is changed.

pg_mystem Extension registration
============
1. Edit your postgresql.conf.
Add the following line - 
`shared_preload_libraries = 'pg_mystem'`
Also you may need to change `max_worker_processes` to `MYSTEM_PROCS` + 1 at least. For example -
`max_worker_processes = 24`

2. Execute the following query inside your database, for example -
```bash
$ sudo -u postgres psql
```
```SQL
\connect YOUR_DB
CREATE EXTENSION pg_mystem;
CREATE FUNCTION mystem_convert(text) RETURNS text AS '$libdir/pg_mystem' LANGUAGE C IMMUTABLE STRICT;
\q 
```
```bash
$ sudo service postgresql restart
```

That's all. Now you can use Mystem from PostgreSQL.
```SQL
SELECT mystem_convert('Ехал грека через реку, сунул грека руку в реку');
                mystem_convert                 
-----------------------------------------------
 ехать грек через река, сунуть грек рука в река +
 
(1 row)
```
