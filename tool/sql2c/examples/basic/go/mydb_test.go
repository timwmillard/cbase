package mydb

import (
	"os"
	"testing"
)

func openTestDB(t *testing.T) *DB {
	t.Helper()
	path := t.TempDir() + "/test.sqlite"
	schema, err := os.ReadFile("../sql/schema.sql")
	if err != nil {
		t.Fatalf("read schema: %v", err)
	}

	db, err := Open(path)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	t.Cleanup(func() { db.Close() })

	// The generated package only exposes the queries in queries.sql, not
	// schema application — apply it directly for the test.
	if err := execSchema(db, string(schema)); err != nil {
		t.Fatalf("apply schema: %v", err)
	}
	return db
}

func TestBoatCRUD(t *testing.T) {
	db := openTestDB(t)

	boat, err := db.CreateBoat(CreateBoatParams{Name: "Reel Deal", Registration: "REG-1"})
	if err != nil {
		t.Fatalf("CreateBoat: %v", err)
	}
	if boat.Name != "Reel Deal" || boat.Registration != "REG-1" || boat.ID == 0 {
		t.Fatalf("unexpected boat: %+v", boat)
	}

	got, err := db.GetBoat(boat.ID)
	if err != nil {
		t.Fatalf("GetBoat: %v", err)
	}
	if got == nil || got.ID != boat.ID || got.Name != boat.Name {
		t.Fatalf("GetBoat mismatch: %+v vs %+v", got, boat)
	}

	missing, err := db.GetBoat(999999)
	if err != nil {
		t.Fatalf("GetBoat(missing): %v", err)
	}
	if missing != nil {
		t.Fatalf("expected nil for missing boat, got %+v", missing)
	}

	list, err := db.ListBoats()
	if err != nil {
		t.Fatalf("ListBoats: %v", err)
	}
	if len(list) != 1 || list[0].ID != boat.ID {
		t.Fatalf("unexpected list: %+v", list)
	}
}

func TestCompetitorNullableRoundTrip(t *testing.T) {
	db := openTestDB(t)

	boat, err := db.CreateBoat(CreateBoatParams{Name: "Reel Deal", Registration: "REG-1"})
	if err != nil {
		t.Fatalf("CreateBoat: %v", err)
	}

	// CreateCompetitor's own params are non-nullable (matches sql2c: bind
	// params are never nullable) — email/boat_id null-ness only shows up on
	// the *result* side, so exercise it via CreateCompetitorNoContact.
	noContact, err := db.CreateCompetitorNoContact(CreateCompetitorNoContactParams{
		FirstName: "Jamie",
		LastName:  "Fisher",
	})
	if err != nil {
		t.Fatalf("CreateCompetitorNoContact: %v", err)
	}
	if noContact.Email != nil {
		t.Fatalf("expected nil Email, got %v", *noContact.Email)
	}
	if noContact.BoatID != nil {
		t.Fatalf("expected nil BoatID, got %v", *noContact.BoatID)
	}

	withContact, err := db.CreateCompetitor(CreateCompetitorParams{
		FirstName: "Alex",
		LastName:  "Angler",
		Email:     "alex@example.com",
		BoatID:    boat.ID,
	})
	if err != nil {
		t.Fatalf("CreateCompetitor: %v", err)
	}
	if withContact.Email == nil || *withContact.Email != "alex@example.com" {
		t.Fatalf("unexpected Email: %v", withContact.Email)
	}
	if withContact.BoatID == nil || *withContact.BoatID != boat.ID {
		t.Fatalf("unexpected BoatID: %v", withContact.BoatID)
	}

	got, err := db.GetCompetitor(withContact.ID)
	if err != nil {
		t.Fatalf("GetCompetitor: %v", err)
	}
	if got == nil || got.Email == nil || *got.Email != "alex@example.com" {
		t.Fatalf("GetCompetitor mismatch: %+v", got)
	}

	if err := db.UpdateCompetitorEmail(UpdateCompetitorEmailParams{
		Email: "alex.angler@example.com",
		ID:    withContact.ID,
	}); err != nil {
		t.Fatalf("UpdateCompetitorEmail: %v", err)
	}
	got, err = db.GetCompetitor(withContact.ID)
	if err != nil {
		t.Fatalf("GetCompetitor after update: %v", err)
	}
	if got.Email == nil || *got.Email != "alex.angler@example.com" {
		t.Fatalf("email not updated: %v", got.Email)
	}

	list, err := db.ListCompetitors()
	if err != nil {
		t.Fatalf("ListCompetitors: %v", err)
	}
	if len(list) != 2 {
		t.Fatalf("expected 2 competitors, got %d", len(list))
	}

	if err := db.DeleteCompetitor(noContact.ID); err != nil {
		t.Fatalf("DeleteCompetitor: %v", err)
	}
	list, err = db.ListCompetitors()
	if err != nil {
		t.Fatalf("ListCompetitors after delete: %v", err)
	}
	if len(list) != 1 {
		t.Fatalf("expected 1 competitor after delete, got %d", len(list))
	}
}

func TestCatchAndClose(t *testing.T) {
	db := openTestDB(t)

	boat, err := db.CreateBoat(CreateBoatParams{Name: "Reel Deal", Registration: "REG-1"})
	if err != nil {
		t.Fatalf("CreateBoat: %v", err)
	}
	competitor, err := db.CreateCompetitorNoContact(CreateCompetitorNoContactParams{
		FirstName: "Jamie",
		LastName:  "Fisher",
	})
	if err != nil {
		t.Fatalf("CreateCompetitorNoContact: %v", err)
	}
	_ = boat

	if _, err := db.CreateCatch(CreateCatchParams{
		CompetitorID: competitor.ID,
		Species:      "Snapper",
		WeightGrams:  2500,
		CaughtAt:     "2026-07-13T09:00:00Z",
	}); err != nil {
		t.Fatalf("CreateCatch: %v", err)
	}

	catches, err := db.ListCatchesByCompetitor(competitor.ID)
	if err != nil {
		t.Fatalf("ListCatchesByCompetitor: %v", err)
	}
	if len(catches) != 1 || catches[0].Species != "Snapper" {
		t.Fatalf("unexpected catches: %+v", catches)
	}

	if err := db.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	if err := db.Close(); err != nil {
		t.Fatalf("second Close should be a no-op, got: %v", err)
	}
	if _, err := db.GetBoat(boat.ID); err != ErrClosed {
		t.Fatalf("expected ErrClosed after Close, got: %v", err)
	}
}
