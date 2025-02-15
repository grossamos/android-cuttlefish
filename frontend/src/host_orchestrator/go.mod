module github.com/google/android-cuttlefish/frontend/src/host_orchestrator

go 1.19

replace github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished => ../liboperator

require (
	github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished
	github.com/google/go-cmp v0.5.9
	github.com/google/uuid v1.3.0
	github.com/gorilla/mux v1.8.0
)

require (
	github.com/hashicorp/errwrap v1.0.0 // indirect
	github.com/hashicorp/go-multierror v1.1.1 // indirect
)
