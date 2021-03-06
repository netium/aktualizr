-- Don't modify this! Create a new migration instead--see docs/schema-migrations.adoc
SAVEPOINT ROLLBACK_MIGRATION;

CREATE TABLE installed_versions_migrate(ecu_serial TEXT NOT NULL, sha256 TEXT NOT NULL, name TEXT NOT NULL, hashes TEXT NOT NULL, length INTEGER NOT NULL DEFAULT 0, correlation_id TEXT NOT NULL DEFAULT '', is_current INTEGER NOT NULL CHECK (is_current IN (0,1)) DEFAULT 0, is_pending INTEGER NOT NULL CHECK (is_pending IN (0,1)) DEFAULT 0, UNIQUE(ecu_serial, sha256, name));
INSERT INTO installed_versions_migrate(ecu_serial, sha256, name, hashes, length, correlation_id, is_current, is_pending) SELECT installed_versions.ecu_serial, installed_versions.sha256, installed_versions.name, installed_versions.hashes, installed_versions.length, installed_versions.correlation_id, installed_versions.is_current, installed_versions.is_pending FROM installed_versions;

DROP TABLE installed_versions;
ALTER TABLE installed_versions_migrate RENAME TO installed_versions;

DELETE FROM version;
INSERT INTO version VALUES(19);

RELEASE ROLLBACK_MIGRATION;
