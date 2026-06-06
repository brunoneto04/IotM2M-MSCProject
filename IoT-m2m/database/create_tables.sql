CREATE TABLE resources
(
    ty INTEGER  NOT NULL,
    ri TEXT     NOT NULL UNIQUE,
    rn TEXT     NOT NULL UNIQUE,
    pi TEXT,
    ct DATETIME NOT NULL,
    lt DATETIME NOT NULL,
    PRIMARY KEY (ri),
    FOREIGN KEY (pi) REFERENCES resources (ri)
);
CREATE TABLE cse_bases
(
    ri  TEXT    NOT NULL UNIQUE,
    csi TEXT    NOT NULL UNIQUE,
    cst INTEGER NOT NULL,
    PRIMARY KEY (ri),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE application_entities
(
    ri  TEXT    NOT NULL UNIQUE,
    api TEXT    NOT NULL UNIQUE,
    aei TEXT    NOT NULL UNIQUE,
    rr  INTEGER NOT NULL,
    et  DATETIME,
    PRIMARY KEY (ri),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE containers
(
    ri  TEXT    NOT NULL UNIQUE,
    et  DATETIME,
    st  INTEGER NOT NULL,
    cni INTEGER NOT NULL,
    cbs INTEGER NOT NULL,
    mbs INTEGER NOT NULL,
    mia INTEGER NOT NULL,
    mni INTEGER NOT NULL,
    PRIMARY KEY (ri),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE content_instances
(
    ri  TEXT    NOT NULL UNIQUE,
    et  DATETIME,
    st  INTEGER NOT NULL,
    con TEXT    NOT NULL,
    cs  INTEGER NOT NULL,
    PRIMARY KEY (ri),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE points_of_access
(
    ri  TEXT NOT NULL,
    poa TEXT NOT NULL,
    PRIMARY KEY (ri, poa),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE supported_resource_types
(
    ri  TEXT    NOT NULL,
    srt INTEGER NOT NULL,
    PRIMARY KEY (ri, srt),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE access_control_policy_ids
(
    ri   TEXT NOT NULL,
    acpi TEXT NOT NULL,
    PRIMARY KEY (ri, acpi),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE content_serializations
(
    ri  TEXT NOT NULL,
    csz TEXT NOT NULL,
    PRIMARY KEY (ri, csz),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE supported_release_versions
(
    ri  TEXT NOT NULL,
    srv TEXT NOT NULL,
    PRIMARY KEY (ri, srv),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE labels
(
    ri  TEXT NOT NULL,
    lbl TEXT NOT NULL,
    PRIMARY KEY (ri, lbl),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);

CREATE TABLE subscriptions
(
    ri                TEXT NOT NULL UNIQUE,
    resource_uri      TEXT NOT NULL,
    notification_uris TEXT NOT NULL,
    notification_type INTEGER,
    event_type        TEXT,
    content_type      TEXT,
    originator        TEXT,
    PRIMARY KEY (ri),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE actions
(
    ri                      TEXT    NOT NULL,
    et                      DATETIME,
    subject_resource_id     TEXT,
    eval_criteria_subject   TEXT    NOT NULL,
    eval_criteria_operator  INTEGER NOT NULL, -- EvalOperator enum
    eval_criteria_threshold TEXT    NOT NULL,
    eval_mode               INTEGER NOT NULL DEFAULT 0,
    eval_control_param      INTEGER,
    object_resource_id      TEXT    NOT NULL,
    action_primitive        TEXT    NOT NULL,
    input                   TEXT,
    action_result           TEXT,
    action_priority         INTEGER,
    PRIMARY KEY (ri),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);
CREATE TABLE schedules
(
    ri  TEXT NOT NULL UNIQUE,
    et  DATETIME,
    sce TEXT NOT NULL,
    PRIMARY KEY (ri),
    FOREIGN KEY (ri) REFERENCES resources (ri)
);