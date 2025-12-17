Find ways to auto delete stuff

Pretext: we have shitload of duplicates we found with a low similarity
threshold, e.g. `p.dht 1`, do I really have to look at each one to be confident
that I'm retaining the right one?

Question: can we we auto-delete anything without even looking at it?

  - the similarity is so good, or validated with `-tm 1` that confidence
    is extremely high
  - we don't care if a false match is dropped... provided that
    some metric between the two images is preserved
  
What metric?
  - one image is a known/established original/source or seems to be
    based on user parameters (creation date etc)
  - bigger (file size or dimensions)
  - less compressed (same codec, lower compression)
  - one seems to have valid metadata, the other has no metadata
  - no-reference quality score based on some model or basic stats
  

Assuming we have all of this, what is the interface

post-filtering that specifies the metric, allowing to preview it with `-show`,
and then pass to `-nuke`


`-with` does not filter needle of the group out, you have got to preserve
  that to visually validate a duplicate pair
  

Case: 
Why: most likely to be a good "replace" situation

-with res "<{%needle}"

Case: show results only if there is a big difference in resolution
Why: 

-with diff:%needle "%inrange{0,10}"

-having -- filters to accept/reject the whole group
 count()
 min(property)
 
-max <property>
-min <property>

