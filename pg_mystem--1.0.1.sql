-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_mystem" to load this file. \quit
CREATE FUNCTION mystem_convert(text) RETURNS text
AS '$libdir/pg_mystem'
LANGUAGE C IMMUTABLE STRICT;
