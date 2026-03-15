CREATE TABLE points_of_access (
    cseBaseID TEXT NOT NULL,
    poa TEXT NOT NULL,
    PRIMARY KEY(cseBaseID, poa)
);
CREATE TABLE resource_types (
    ty TEXT NOT NULL,
    name TEXT NOT NULL UNIQUE,
    PRIMARY KEY(ty)
);
CREATE TABLE supported_resource_types (
    cseBaseID TEXT NOT NULL,
    srt TEXT NOT NULL,
    PRIMARY KEY(cseBaseID, srt)
);
CREATE TABLE labels (
    ri TEXT NOT NULL,
    lbl TEXT NOT NULL,
    PRIMARY KEY(ri, lbl)
);
CREATE TABLE cse_bases (
    ri TEXT NOT NULL UNIQUE,
    ty INTEGER NOT NULL,
    rn TEXT NOT NULL UNIQUE,
    pi TEXT,
    ct DATETIME NOT NULL,
    lt DATETIME NOT NULL,
    csi TEXT NOT NULL UNIQUE,
    PRIMARY KEY(ri),
    FOREIGN KEY(ri) REFERENCES supported_resource_types(cseBaseID),
    FOREIGN KEY(ri) REFERENCES points_of_access(cseBaseID),
    FOREIGN KEY(ty) REFERENCES resource_types(ty)
);
CREATE TABLE application_entities (
    ri TEXT NOT NULL UNIQUE,
    ty TEXT NOT NULL,
    rn TEXT NOT NULL UNIQUE,
    pi INTEGER NOT NULL,
    et DATETIME NOT NULL,
    ct DATETIME NOT NULL,
    lt DATETIME NOT NULL,
    api TEXT NOT NULL UNIQUE,
    aei TEXT NOT NULL UNIQUE,
    rr INTEGER NOT NULL,
    PRIMARY KEY(ri),
    FOREIGN KEY(ri) REFERENCES labels(ri),
    FOREIGN KEY(pi) REFERENCES cse_bases(ri)
);
CREATE TABLE containers (
    ri TEXT NOT NULL,
    ty TEXT NOT NULL,
    rn TEXT NOT NULL,
    pi INTEGER NOT NULL,
    et DATETIME NOT NULL,
    ct DATETIME NOT NULL,
    lt DATETIME NOT NULL,
    st TEXT NOT NULL,
    cni INTEGER NOT NULL,
    cbs INTEGER NOT NULL,
    PRIMARY KEY(ri)
);
