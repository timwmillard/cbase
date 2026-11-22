
create table if not exists person (
    id integer primary key,
    name text not null default '',
    age integer not null default 0
);

create table if not exists pet (
    id integer primary key,
    name text not null default '',
    owner_id integer references person(id)
);
