// basic function for loading, uploading, encoding, decoding lowres chunks
// stores point clouds for each child chunk
// stores id
// stores layer
// constructor calls load
// load
//      use chunk map (depending on layer) to get child ids for chunk id
//      load all of the point clouds for child chunk ids from fs in memory
// update
//      run kmeans on point clouds, saves box geometry data to r2 bucket
// save
//      samples points and saves them to fs under chunk id (unless layer 0)