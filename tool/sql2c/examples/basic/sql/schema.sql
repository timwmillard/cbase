-- Boat
create table boat (
    id integer primary key,
    name text not null,
    registration text not null
);

-- Competitor
create table competitor (
    id integer primary key,
    first_name text not null,
    last_name text not null,
    email text,
    boat_id integer references boat(id)
);

-- Catch
create table catch (
    id integer primary key,
    competitor_id integer not null references competitor(id),
    species text not null,
    weight_grams integer not null,
    caught_at text not null
);
