-- name: CreateBoat :one
insert into boat (name, registration)
values (:name, :registration)
returning *;

-- name: GetBoat :one
select * from boat where id = :id;

-- name: ListBoats :many
select * from boat order by name;

-- name: CreateCompetitor :one
insert into competitor (first_name, last_name, email, boat_id)
values (:first_name, :last_name, :email, :boat_id)
returning *;

-- name: CreateCompetitorNoContact :one
insert into competitor (first_name, last_name)
values (:first_name, :last_name)
returning *;

-- name: GetCompetitor :one
select * from competitor where id = :id;

-- name: ListCompetitors :many
select * from competitor order by last_name, first_name;

-- name: UpdateCompetitorEmail :exec
update competitor set email = :email where id = :id;

-- name: DeleteCompetitor :exec
delete from competitor where id = :id;

-- name: CreateCatch :one
insert into catch (competitor_id, species, weight_grams, caught_at)
values (:competitor_id, :species, :weight_grams, :caught_at)
returning *;

-- name: ListCatchesByCompetitor :many
select * from catch where competitor_id = :competitor_id order by caught_at;
