-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION mtree" to load this file. \quit

CREATE FUNCTION mtreehandler(internal)
RETURNS index_am_handler
AS 'MODULE_PATHNAME'
LANGUAGE C;

-- Access method
CREATE ACCESS METHOD mtree TYPE INDEX HANDLER mtreehandler;
COMMENT ON ACCESS METHOD mtree IS 'mtree index access method';