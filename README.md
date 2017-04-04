# **pg_mystem - расширение PostgreSQL для поддержки Yandex Mystem**
`pg_mystem` - [расширение PostgreSQL](https://www.postgresql.org/docs/9.6/static/extend-extensions.html) для [Yandex mystem](https://tech.yandex.ru/mystem/) (стеммер или лемматизатор или морфологический нормализатор - точный термин, как я понимаю, в русском языке не определен). Основная функция данного расширения заключается в объединении лучшего, на мой взгляд, стеммера русского языка `mystem` и мощнейшей СУБД `PostgreSQL`.
### Установка Yandex Mystem
Необходимо загрузить исполнимый файл [mystem](https://tech.yandex.ru/mystem/), соответствующий вашей архитектуре и скопировать его в share директорию `PostgreSQL`.  
Пример:
```bash
$ wget http://download.cdn.yandex.net/mystem/mystem-3.0-linux3.1-64bit.tar.gz
$ tar xfz mystem-3.0-linux3.1-64bit.tar.gz
$ sudo cp ./mystem `pg_config --sharedir`
```
### Установка pg_mystem
```bash
$ git clone https://github.com/maxoodf/pg_mystem.git
$ cd ./pg_mystem
$ git submodule init
$ git submodule update
$ make
$ sudo make install
```
### Настройка pg_mystem
Настройки `pg_mystem` могут быть изменены. Для этого необходимо отредактировать соответствующие переменные, определенные в Makefile  -
1. `DOC_LEN_MAX` - максимальный размер документа (в байтах). Если вы работаете с короткими документами (строками), установите значение `DOC_LEN_MAX`, например, в 1000. Для работы с большими документами (строками), установите `DOC_LEN_MAX` в необходимое значение.
2. `MYSTEM_PROCS` - количество запущенных `mystem` процессов. Рекомендованное значение - один `mystem` процесс на 9 KB/sec обрабатываемого текста. Например, если требуется обеспечить производительность лемматизации в 50KB текста в секунду, используйте 6 процессов `mystem` (приведенные значения являются крайне относительными и зависят от производительности вашей системы).

После изменения настроек необходимо переустановить `pg_mystem`, как описано ранее.
### Регистрация расширения pg_mystem
1. Измените ваш конфигурационный файл `postgresql.conf`.
  - необходимо добавить следующую строку - `shared_preload_libraries = 'pg_mystem'`  
  - возможно вам потребуется увелисить количество `max_worker_processes` до `MYSTEM_PROCS` + 1, как минимум. Например, строка конфигурационного файла - `max_worker_processes = 24`
2. Перезапустите `PostgreSQL`
```bash
$ sudo service postgresql restart
```
3. Выполните следующий запрос для регистрации расширения в `PostgreSQL` -
```bash
$ sudo -u postgres psql
```
```SQL
\connect YOUR_DB
CREATE EXTENSION pg_mystem;
\q
```

Теперь вы можете использовать `mystem` из `PostgreSQL`.
```SQL
SELECT mystem_convert('Ехал грека через реку, сунул грека руку в реку');
                mystem_convert                 
-----------------------------------------------
 ехать грек через река, сунуть грек рука в река +

(1 row)
```

# **pg_mystem - PostgreSQL extension for Yandex Mystem**
`pg_mystem` is an implementation of the [PostgreSQL extension](https://www.postgresql.org/docs/9.6/static/extend-extensions.html) for [Yandex mystem](https://tech.yandex.ru/mystem/) (morphology analyzer/stemmer for Russian language). What is the extension function? You can use the power of the `mystem` inside of a `PostgreSQL` database.
### Yandex mystem Installation
Download binary [mystem](https://tech.yandex.ru/mystem/) file and install it to the `PostgreSQL` share directory.  
Example:
```bash
$ wget http://download.cdn.yandex.net/mystem/mystem-3.0-linux3.1-64bit.tar.gz
$ tar xfz mystem-3.0-linux3.1-64bit.tar.gz
$ sudo cp ./mystem `pg_config --sharedir`
```

### pg_mystem Installation
```bash
$ git clone https://github.com/maxoodf/pg_mystem.git
$ cd ./pg_mystem
$ git submodule init
$ git submodule update
$ make
$ sudo make install
```

### pg_mystem Configuration
You may wish to change `pg_mystem` default settings. All you need is to change Makefile defined parameters -
1. `DOC_LEN_MAX` - maximum document (string) length. If you work with a short lines, redefine `DOC_LEN_MAX` to 1000 chars or so. If you work with a large documents, redefine `DOC_LEN_MAX` to 100000 characters etc.
2. `MYSTEM_PROCS` - how many `mystem` processes to run. I use the following value in my projects - one `mystem` process throughput is about 9 KB/sec (depends on hardware). So if I need to process, say 50 KB of text in a second I use 6 `mystem` processes.

You will need to reinstall `pg_mystem` in case any of these parameters is changed.

### pg_mystem Extension registration
1. Edit your `postgresql.conf`.  
Add the following line - `shared_preload_libraries = 'pg_mystem'`  
Also you may need to change `max_worker_processes` to `MYSTEM_PROCS` + 1 at least.  
Example - `max_worker_processes = 24`
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

That's all. Now you can use `mystem` from `PostgreSQL` queries.
```SQL
SELECT mystem_convert('Ехал грека через реку, сунул грека руку в реку');
                mystem_convert                 
-----------------------------------------------
 ехать грек через река, сунуть грек рука в река +

(1 row)
```
