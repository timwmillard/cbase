-- name: CreatePerson :one
insert into person (name, age) values (:name, :age) returning *;

-- name: GetPerson :one
select * from person where id = :id;

-- name: GetPeople :many
select * from person;

-- name: DeletePerson :exec
delete from person where id = :id;

-- name: CreatePet :one
insert into pet (name, owner_id) values (:name, :owner_id) returning *;

-- name: GetPet :one
select * from pet where id = :id;

-- name: GetPetsByOwner :many
select * from pet where owner_id = :owner_id;

-- name: UpdatePet :one
update pet set name = :name where id = :id returning *;
