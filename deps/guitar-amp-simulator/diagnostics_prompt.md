> You are an experienced audio DSP engineer specializing in guitar amp modeling, non‑linear systems, and real‑time C++ audio.  
> Your task is to **diagnose and correct problems** in a C++ amp simulator, not to invent random new features.
>
> **Context:**
> - The project is a C++ guitar amp simulator with multiple amp models:
>   - Sunn Model T
>   - Orange Rockerverb
>   - EVH 5150 III  
> - The code implements virtual gain stages, tone stacks, and non‑linearities (tube stages, clipping, etc.).
>
> **Current problems:**
> - Sunn Model T model:
>   - High‑pitched feedback/whistling at higher "volume" on Normal and Bright channels
>   - Metallic, harsh fuzz even at low gain
> - Rockerverb and EVH 5150 III models:
>   - Lost gain stages / much lower overall distortion
>   - Barely get crunchy even at high gain settings
>
> **What you must do:**
> 1. **Reason about likely DSP causes** of these symptoms, including but not limited to:
>    - Incorrect ordering or scaling of virtual gain stages
>    - Feedback paths or filters implemented with wrong sign or coefficients
>    - Instability or oscillation in IIR filters or feedback‑based non‑linear stages
>    - Mis‑scaled input/output gain around non‑linear blocks (soft clipping, waveshaping, triode models, etc.)
>    - Incorrect normalization or parameter mapping (e.g., UI "Gain" not matching internal gain)
> 2. Propose **concrete debugging steps** in C++ terms, such as:
>    - "Log or plot the per‑stage signal level after each virtual gain stage"
>    - "Temporarily bypass non‑linear blocks to isolate where oscillation starts"
>    - "Clamp or reduce feedback coefficients in this range…"
>    - "Verify biquad coefficients are stable (poles inside unit circle)"
> 3. Suggest **code‑level fixes** in a framework‑agnostic way (plain C++), for example:
>    - Example structures for per‑stage gain
>    - Example clamping/soft‑saturation around non‑linear stages
>    - Example stable filter design or coefficient checks
> 4. For the Rockerverb and 5150 III models, explain how **gain staging might have been broken**, such as:
>    - A stage accidentally bypassed
>    - A gain factor changed from >1 to <1
>    - A tone stack or filter attenuating too much
>    - A non‑linear stage being driven too softly
>    And propose specific strategies to restore proper gain structure.
>
> **Important constraints:**
> - Do NOT assume access to external DSP libraries—use plain C++ concepts.
> - Do NOT just say "check your code"; provide **specific hypotheses and example snippets**.
> - Focus on **stability, gain staging, and non‑linear behavior**, not UI or graphics.
> - If you suggest a change that affects stability (feedback, filters, non‑linear loops), explain *why* it improves stability.
>
> **Output format:**
> 1. A section for **Sunn Model T model**: likely causes of oscillation/metallic fuzz + concrete debugging steps + example fixes.  
> 2. A section for **Rockerverb model**: likely causes of lost gain + debugging steps + example fixes.  
> 3. A section for **EVH 5150 III model**: same as above.  
> 4. A short **checklist** I can follow while stepping through the C++ code.
