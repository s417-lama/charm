module MPIINCL
   include 'ampif.h'
   
   external MPI_Init_universe
   external MPI_Comm_rank
   external MPI_Comm_size
   external MPI_Finalize
   external MPI_Send
   external MPI_Ssend
   external MPI_Recv
   external MPI_Isend
   external MPI_Issend
   external MPI_Irecv
   external MPI_Sendrecv
   external MPI_Barrier
   external MPI_Bcast
   external MPI_Reduce
   external MPI_Allreduce
   external MPI_Start
   external MPI_Waitall
   external MPI_Waitany
   external MPI_Wait
   external MPI_Send_init
   external MPI_Recv_init
   external MPI_Type_contiguous
   external MPI_Type_vector
   external MPI_Type_hvector
   external MPI_Type_indexed
   external MPI_Type_hindexed
   external MPI_Type_struct
   external MPI_Type_commit
   external MPI_Type_free
   external MPI_Type_extent
   external MPI_Type_size
   external MPI_Allgatherv
   external MPI_Allgather
   external MPI_Gatherv
   external MPI_Gather
   external MPI_Alltoallv
   external MPI_Alltoall
   external MPI_Comm_dup
   external MPI_Comm_free
   external MPI_Abort
   external MPI_Print
   external MPI_Migrate

   external MPI_Register_main
   external MPI_Attach

contains

  subroutine MPI_Init(ierr)
    integer :: ierr
    call MPI_Init_universe(MPI_COMM_UNIVERSE)
    ierr = 0
  end subroutine

end module MPIINCL
