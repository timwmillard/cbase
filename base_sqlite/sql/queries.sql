
-- name: CreatePerson :one
insert into person (name, age) values (?, ?) returning *;

-- name: GetPerson :one
select * from person where id = ?;

-- name: GetPeople :many
select * from person;

-- name: DeletePerson :exec
delete from person where id = ?;

-- name: CreatePet :one
insert into pet (name, owner_id) values (?, ?) returning *;

-- name: GetPet :one
select * from pet where id = ?;

-- name: GetPetsByOwner :many
select * from pet where owner_id = ?;

-- name: UpdatePet :one
update pet set name = ? where id = ? returning *;
