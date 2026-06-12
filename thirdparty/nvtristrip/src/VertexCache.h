  
#ifndef VERTEX_CACHE_H

#define VERTEX_CACHE_H

class VertexCache
{
	
public:
	
	VertexCache(int size)
	{
		numEntries = size;
		
		entries = new int[numEntries];
		
		for(int i = 0; i < numEntries; i++)
			entries[i] = -1;
	}
		
	VertexCache() { VertexCache(16); }
	~VertexCache() { delete[] entries; entries = 0; }
	
	bool InCache(int entry);
	int AddEntry(int entry);
	void Clear();
	void Copy(VertexCache* inVcache);
	int At(int index) { return entries[index]; }
	void Set(int index, int value) { entries[index] = value; }

private:

  int *entries;
  int numEntries;

};

#endif
