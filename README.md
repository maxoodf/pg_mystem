pg_mystem - PostgreSQL extension for Yandex Mystem.
============

`pg_mystem` is an implementation of the `PostgreSQL` extension for Yandex `mystem` (morphology analyzer/stemmer for Russian language).

Read more about `PostgreSQL` extensions here -  https://www.postgresql.org/docs/9.5/static/extend-extensions.html and here - https://www.postgresql.org/docs/9.5/static/extend-pgxs.html

Read more about Yandex `mystem` - https://tech.yandex.ru/mystem/

What is the extension function? You can use the power of `mystem` inside your `PostgreSQL` database.

Yandex mystem Installation
============
Download binary file from https://tech.yandex.ru/mystem/ and install it to the `PostgreSQL` share directory. 
Example:
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
$ git submodule init
$ git submodule update
$ make
$ sudo make install
```

pg_mystem Configuration
============
You may wish to change `pg_mystem` default settings. All you need is to change Makefile defined parameters - 

1. `DOC_LEN_MAX` - maximum document (string) length. If you work with a short lines, redefine `DOC_LEN_MAX` to 1000 chars or so. If you work with a large documents, redefine `DOC_LEN_MAX` to 100000 characters etc.

2. `MYSTEM_PROCS` - how many `mystem` processes to run. I use the following value in my projects - one `mystem` process throughput is about 9 KB/sec (depends on hardware). So if I need to process, say 50 KB of text in a second I use 6 `mystem` processes.

You will need to reinstall `pg_mystem` in case any of these parameters is changed.

pg_mystem Extension registration
============
1. Edit your `postgresql.conf`.
Add the following line - 
`shared_preload_libraries = 'pg_mystem'`
Also you may need to change `max_worker_processes` to `MYSTEM_PROCS` + 1 at least. 
Example -
`max_worker_processes = 24`

2. Restart `PostgreSQL`
```bash
$ sudo service postgresql restart
```

3. Execute the following query inside your database.
Example -
```bash
$ sudo -u postgres psql
```
```SQL
\connect YOUR_DB
CREATE EXTENSION pg_mystem;
\q 
```

That's all. Now you can use `mystem` from `PostgreSQL`.
```SQL
SELECT mystem_convert('Ехал грека через реку, сунул грека руку в реку');
                mystem_convert                 
-----------------------------------------------
 ехать грек через река, сунуть грек рука в река +
 
(1 row)
```
