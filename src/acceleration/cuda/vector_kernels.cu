// CUDA Kernels for Vector Operations
// ThemisDB Hardware Acceleration

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cmath>

namespace themis {
namespace acceleration {
namespace cuda {

// ============================================================================
// Distance Computation Kernels
// ============================================================================

/**
 * Compute L2 (Euclidean) distance between query vectors and database vectors
 * 
 * @param queries      Query vectors (numQueries x dim)
 * @param vectors      Database vectors (numVectors x dim)
 * @param distances    Output distances (numQueries x numVectors)
 * @param numQueries   Number of query vectors
 * @param numVectors   Number of database vectors
 * @param dim          Vector dimension
 */
__global__ void computeL2DistanceKernel(
    const float* queries,
    const float* vectors,
    float* distances,
    int numQueries,
    int numVectors,
    int dim
) {
    int qIdx = blockIdx.y * blockDim.y + threadIdx.y;  // Query index
    int vIdx = blockIdx.x * blockDim.x + threadIdx.x;  // Vector index
    
    if (qIdx >= numQueries || vIdx >= numVectors) return;
    
    const float* query = queries + qIdx * dim;
    const float* vector = vectors + vIdx * dim;
    
    float sum = 0.0f;
    
    // Compute squared L2 distance
    #pragma unroll 4
    for (int i = 0; i < dim; i++) {
        float diff = query[i] - vector[i];
        sum += diff * diff;
    }
    
    // Store sqrt(sum) for L2 distance
    distances[qIdx * numVectors + vIdx] = sqrtf(sum);
}

/**
 * Compute Cosine distance between query vectors and database vectors
 * Distance = 1 - cosine_similarity
 * 
 * @param queries      Query vectors (numQueries x dim)
 * @param vectors      Database vectors (numVectors x dim)
 * @param distances    Output distances (numQueries x numVectors)
 * @param numQueries   Number of query vectors
 * @param numVectors   Number of database vectors
 * @param dim          Vector dimension
 */
__global__ void computeCosineDistanceKernel(
    const float* queries,
    const float* vectors,
    float* distances,
    int numQueries,
    int numVectors,
    int dim
) {
    int qIdx = blockIdx.y * blockDim.y + threadIdx.y;
    int vIdx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (qIdx >= numQueries || vIdx >= numVectors) return;
    
    const float* query = queries + qIdx * dim;
    const float* vector = vectors + vIdx * dim;
    
    float dotProduct = 0.0f;
    float normQuery = 0.0f;
    float normVector = 0.0f;
    
    #pragma unroll 4
    for (int i = 0; i < dim; i++) {
        float q = query[i];
        float v = vector[i];
        dotProduct += q * v;
        normQuery += q * q;
        normVector += v * v;
    }
    
    normQuery = sqrtf(normQuery);
    normVector = sqrtf(normVector);
    
    float cosineSim = (normQuery > 1e-10f && normVector > 1e-10f) 
        ? dotProduct / (normQuery * normVector)
        : 0.0f;
    
    // Store 1 - cosine_similarity as distance
    distances[qIdx * numVectors + vIdx] = 1.0f - cosineSim;
}

// ============================================================================
// Top-K Selection Kernels (for KNN)
// ============================================================================

/**
 * Bitonic sort for finding top-k elements
 * Used for small k values
 */
__device__ void bitonicSortStep(
    int* indices,
    float* values,
    int k,
    int j,
    int dir
) {
    int i = threadIdx.x;
    if (i >= k) return;
    
    int ixj = i ^ j;
    if (ixj > i) {
        if ((i & dir) == 0) {
            // Ascending
            if (values[i] > values[ixj]) {
                // Swap
                float tempVal = values[i];
                values[i] = values[ixj];
                values[ixj] = tempVal;
                
                int tempIdx = indices[i];
                indices[i] = indices[ixj];
                indices[ixj] = tempIdx;
            }
        } else {
            // Descending
            if (values[i] < values[ixj]) {
                float tempVal = values[i];
                values[i] = values[ixj];
                values[ixj] = tempVal;
                
                int tempIdx = indices[i];
                indices[i] = indices[ixj];
                indices[ixj] = tempIdx;
            }
        }
    }
}

/**
 * Extract top-k nearest neighbors for each query
 * 
 * @param distances       Distance matrix (numQueries x numVectors)
 * @param topkIndices     Output: top-k indices (numQueries x k)
 * @param topkDistances   Output: top-k distances (numQueries x k)
 * @param numQueries      Number of queries
 * @param numVectors      Number of vectors
 * @param k               Number of nearest neighbors
 */
__global__ void extractTopKKernel(
    const float* distances,
    int* topkIndices,
    float* topkDistances,
    int numQueries,
    int numVectors,
    int k
) {
    extern __shared__ char sharedMem[];
    float* sharedDist = (float*)sharedMem;
    int* sharedIdx = (int*)(sharedMem + k * sizeof(float));
    
    int qIdx = blockIdx.x;
    if (qIdx >= numQueries) return;
    
    const float* queryDistances = distances + qIdx * numVectors;
    
    // Initialize shared memory with first k elements
    if (threadIdx.x < k) {
        sharedDist[threadIdx.x] = (threadIdx.x < numVectors) 
            ? queryDistances[threadIdx.x] 
            : INFINITY;
        sharedIdx[threadIdx.x] = threadIdx.x;
    }
    __syncthreads();
    
    // Process remaining elements
    for (int i = k + threadIdx.x; i < numVectors; i += blockDim.x) {
        float dist = queryDistances[i];
        
        // Find max in current top-k
        if (threadIdx.x == 0) {
            int maxIdx = 0;
            float maxDist = sharedDist[0];
            for (int j = 1; j < k; j++) {
                if (sharedDist[j] > maxDist) {
                    maxDist = sharedDist[j];
                    maxIdx = j;
                }
            }
            
            // Replace if current distance is smaller
            if (dist < maxDist) {
                sharedDist[maxIdx] = dist;
                sharedIdx[maxIdx] = i;
            }
        }
        __syncthreads();
    }
    
    // Sort top-k (bitonic sort for small k)
    if (k <= 1024) {
        for (int size = 2; size <= k; size *= 2) {
            int dir = (threadIdx.x & (size / 2)) == 0 ? 0 : size;
            for (int stride = size / 2; stride > 0; stride /= 2) {
                bitonicSortStep(sharedIdx, sharedDist, k, stride, dir);
                __syncthreads();
            }
        }
    }
    
    // Write results
    if (threadIdx.x < k) {
        topkIndices[qIdx * k + threadIdx.x] = sharedIdx[threadIdx.x];
        topkDistances[qIdx * k + threadIdx.x] = sharedDist[threadIdx.x];
    }
}

// ============================================================================
// Kernel Launchers (C++ interface)
// ============================================================================

extern "C" {

/**
 * Launch L2 distance computation kernel
 */
void launchL2DistanceKernel(
    const float* d_queries,
    const float* d_vectors,
    float* d_distances,
    int numQueries,
    int numVectors,
    int dim,
    cudaStream_t stream
) {
    dim3 blockDim(16, 16);
    dim3 gridDim(
        (numVectors + blockDim.x - 1) / blockDim.x,
        (numQueries + blockDim.y - 1) / blockDim.y
    );
    
    computeL2DistanceKernel<<<gridDim, blockDim, 0, stream>>>(
        d_queries, d_vectors, d_distances,
        numQueries, numVectors, dim
    );
}

/**
 * Launch Cosine distance computation kernel
 */
void launchCosineDistanceKernel(
    const float* d_queries,
    const float* d_vectors,
    float* d_distances,
    int numQueries,
    int numVectors,
    int dim,
    cudaStream_t stream
) {
    dim3 blockDim(16, 16);
    dim3 gridDim(
        (numVectors + blockDim.x - 1) / blockDim.x,
        (numQueries + blockDim.y - 1) / blockDim.y
    );
    
    computeCosineDistanceKernel<<<gridDim, blockDim, 0, stream>>>(
        d_queries, d_vectors, d_distances,
        numQueries, numVectors, dim
    );
}

/**
 * Launch top-k extraction kernel
 */
void launchTopKKernel(
    const float* d_distances,
    int* d_topkIndices,
    float* d_topkDistances,
    int numQueries,
    int numVectors,
    int k,
    cudaStream_t stream
) {
    int threadsPerBlock = 256;
    int sharedMemSize = k * (sizeof(float) + sizeof(int));
    
    extractTopKKernel<<<numQueries, threadsPerBlock, sharedMemSize, stream>>>(
        d_distances, d_topkIndices, d_topkDistances,
        numQueries, numVectors, k
    );
}

} // extern "C"

} // namespace cuda
} // namespace acceleration
} // namespace themis
