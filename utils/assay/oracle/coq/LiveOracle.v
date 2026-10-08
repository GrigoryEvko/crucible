(* Liveness certificates for the session oracle.

   The development of Keskin, Yoshida and van Glabbeek (github.com/omerskeskin/mpstlive,
   "Formally Verified Liveness with Multiparty Session Types in Rocq") proves

     liveness : forall gamma g, wfgC g -> projectableA g -> tctx_wf gamma ->
                assoc gamma g -> liveCtx gamma.

   This file proves the four premises for a finite-state global type and a typing context
   from data that a boolean check accepts.  Each certificate states its data, a coinductive
   tree for each state, one unfolding lemma per tree, and then applies [certificate_live],
   whose check runs by computation.  So every proof step lives here, once, and a certificate
   holds no proof script of its own.

   The data describe:
   - the states of the global type as closed recursion terms [d_terms] (the witness of wfgC),
     whose one-step unfolding gives the head and the children in [d_g];
   - the participants [d_parts], and for each participant and state a rank [d_rank]: None when
     the participant never acts below the state, Some r when every path acts within r steps
     (the balance property balancedG);
   - one list of local states [d_l] that holds the development's plain-merge projection of each
     participant at each state ([d_proj]) and the types of the context under test;
   - the context [d_ctx] as pairs of a role and a local state;
   - a subtyping relation [d_sub] over local states that relates each context entry to the
     projection of its participant (association). *)

Require Import List Arith Lia Bool.
From Paco Require Import paco.
Import ListNotations.
From live_mpst.STBase Require Import src.header src.expr src.local src.global src.balanced
  src.part src.projection src.merge.
From live_mpst.STBase Require Import lemma.decidable_helper lemma.decidable.
From live_mpst.STLive Require Import src.lcontext src.assoc src.wfltt src.path_props lemma.liveness.
From mathcomp Require ssrnat.

(* The development states its order premises with the boolean order of mathcomp. *)
Lemma to_leq : forall a b, a <= b -> is_true (ssrnat.leq a b).
Proof. intros a b H. destruct (@ssrnat.leP a b) as [_|Hn]; [reflexivity | contradiction]. Qed.

(* ── Terms ─────────────────────────────────────────────────────────── *)

Fixpoint gsubst (t m : fin) (G P : global) : global :=
  match P with
  | g_var n =>
      if Nat.eqb n t then incr_freeG 0 m G
      else match n with
           | 0 => g_var 0
           | S n' => if Nat.leb t n' then g_var n' else g_var (S n')
           end
  | g_end => g_end
  | g_send p q xs =>
      g_send p q (map (fun u => match u with
                                | Some (s, g) => Some (s, gsubst t m G g)
                                | None => None
                                end) xs)
  | g_rec g => g_rec (gsubst (S t) (S m) G g)
  end.

Lemma gsubst_correct : forall P t m G, subst_global t m G P (gsubst t m G P).
Proof.
  intro P. induction P using global_ind_ref; intros t m G; simpl.
  - destruct (Nat.eqb n t) eqn:He.
    + apply Nat.eqb_eq in He; subst. apply subg_var_is.
    + apply Nat.eqb_neq in He. destruct n as [|n'].
      * apply subg_var_notz. lia.
      * destruct (Nat.leb t n') eqn:Hl.
        { apply Nat.leb_le in Hl. apply subg_var_not1; [lia | apply to_leq; lia]. }
        { apply Nat.leb_gt in Hl. apply subg_var_not2; [lia | apply to_leq; lia]. }
  - apply subg_end.
  - apply subg_send. induction H as [|u lis Hu Hlis IH]; simpl; constructor; auto.
    destruct Hu as [-> | (s & g & -> & Hg)]; [left; split; reflexivity | right].
    exists s, g, (gsubst t m G g). split; [reflexivity | split; [reflexivity | apply Hg]].
  - apply subg_rec. apply IHP.
Qed.

Definition kid_terms (xs : list (option (sort * global))) : list global :=
  flat_map (fun u => match u with Some (_, g) => [g] | None => [] end) xs.

Fixpoint cl (k : nat) (P : global) : bool :=
  match P with
  | g_var n => Nat.ltb n k
  | g_end => true
  | g_send _ _ xs =>
      forallb (fun u => match u with Some (_, g) => cl k g | None => true end) xs
  | g_rec g => cl (S k) g
  end.

Fixpoint shaped (P : global) : bool :=
  match P with
  | g_var _ => true
  | g_end => true
  | g_send _ _ xs =>
      forallb (fun u => match u with Some (_, g) => shaped g | None => true end) xs
  | g_rec g =>
      match g with
      | g_send _ _ _ => shaped g
      | g_end => true
      | _ => false
      end
  end.

Fixpoint slistb {A} (l : list (option A)) : bool :=
  match l with
  | [Some _] => true
  | _ :: l' => slistb l'
  | [] => false
  end.

Lemma slistb_sound : forall A (l : list (option A)), slistb l = true -> SList l.
Proof.
  intros A l. induction l as [|a l IH]; simpl; [discriminate|].
  destruct a as [a|]; destruct l as [|b l]; simpl; auto.
Qed.

Fixpoint wfb (P : global) : bool :=
  match P with
  | g_var _ => true
  | g_end => true
  | g_send p q xs =>
      slistb xs && negb (Nat.eqb p q)
      && forallb (fun u => match u with Some (_, g) => wfb g | None => true end) xs
  | g_rec g => wfb g
  end.

Lemma forallb_opt : forall (f : global -> bool) (xs : list (option (sort * global))),
  forallb (fun u => match u with Some (_, g) => f g | None => true end) xs = true ->
  Forall (fun u => u = None \/ exists s g, u = Some (s, g) /\ f g = true) xs.
Proof.
  intros f xs. induction xs as [|u xs IH]; simpl; auto.
  intro H. apply andb_prop in H as [Hu Hxs]. constructor; auto.
  destruct u as [[s g]|]; [right; exists s, g; auto | left; auto].
Qed.

Lemma wfb_sound : forall P, wfb P = true -> wfG P.
Proof.
  intro P. induction P using global_ind_ref; simpl; intro Hw.
  - apply wfg_var.
  - apply wfg_end.
  - apply andb_prop in Hw as [Hw Hk]. apply andb_prop in Hw as [Hs Hpq].
    apply wfg_send.
    + now apply slistb_sound.
    + apply negb_true_iff in Hpq. now apply Nat.eqb_neq in Hpq.
    + apply forallb_opt in Hk. clear Hs Hpq.
      induction H as [|u lis Hu Hlis IH]; constructor; inversion Hk; subst; auto.
      destruct Hu as [-> | (s & g & -> & Hg)]; [left; auto|].
      destruct H1 as [Hn | (s' & g' & He & Hw')]; [discriminate|].
      injection He as <- <-. right. exists s, g. auto.
  - apply wfg_rec. auto.
Qed.

(* incr_freeG leaves a term alone whose free variables are all below the cut. *)
Lemma incr_closed : forall P k m, cl k P = true -> incr_freeG k m P = P.
Proof.
  intro P. induction P using global_ind_ref; simpl; intros k m Hc.
  - apply Nat.ltb_lt in Hc. destruct (@ssrnat.leP k n) as [Hl|Hl]; [lia | reflexivity].
  - reflexivity.
  - f_equal. apply forallb_opt in Hc. induction H as [|u lis Hu Hlis IH]; simpl; auto.
    inversion Hc; subst. f_equal; auto.
    destruct Hu as [-> | (s & g & -> & Hg)]; auto.
    destruct H1 as [Hn | (s' & g' & He & Hg')]; [discriminate|].
    injection He as <- <-. now rewrite Hg.
  - f_equal. auto.
Qed.

Lemma cl_weaken : forall P k k', cl k P = true -> k <= k' -> cl k' P = true.
Proof.
  intro P. induction P using global_ind_ref; simpl; intros k k' Hc Hk.
  - apply Nat.ltb_lt in Hc. apply Nat.ltb_lt. lia.
  - auto.
  - apply forallb_opt in Hc. apply forallb_forall. intros u Hin.
    rewrite Forall_forall in H, Hc. specialize (H u Hin). specialize (Hc u Hin).
    destruct H as [-> | (s & g & -> & Hg)]; auto.
    destruct Hc as [Hn | (s' & g' & He & Hg')]; [discriminate|].
    injection He as <- <-. eapply Hg; eauto.
  - eapply IHP; eauto. lia.
Qed.

(* Substituting a closed term for the variable t closes a body closed at t + 1. *)
Lemma gsubst_cl : forall P t m G, cl (S t) P = true -> cl 0 G = true -> cl t (gsubst t m G P) = true.
Proof.
  intro P. induction P using global_ind_ref; simpl; intros t m G Hc HG.
  - apply Nat.ltb_lt in Hc. destruct (Nat.eqb n t) eqn:He.
    + rewrite incr_closed; auto. eapply cl_weaken; eauto. lia.
    + apply Nat.eqb_neq in He. destruct n as [|n'].
      * simpl. apply Nat.ltb_lt. lia.
      * destruct (Nat.leb t n') eqn:Hl.
        { apply Nat.leb_le in Hl. lia. }
        { simpl. apply Nat.ltb_lt. lia. }
  - auto.
  - apply forallb_opt in Hc. apply forallb_forall. intros u Hin.
    apply in_map_iff in Hin as (u0 & <- & Hin0).
    rewrite Forall_forall in H, Hc. specialize (H u0 Hin0). specialize (Hc u0 Hin0).
    destruct H as [-> | (s & g & -> & Hg)]; auto.
    destruct Hc as [Hn | (s' & g' & He & Hg')]; [discriminate|].
    injection He as <- <-. auto.
  - apply IHP; auto.
Qed.

Lemma gsubst_shaped : forall P t m G, shaped P = true -> shaped G = true -> cl 0 G = true ->
  shaped (gsubst t m G P) = true.
Proof.
  intro P. induction P using global_ind_ref; simpl; intros t m G Hs HG Hc.
  - destruct (Nat.eqb n t); [rewrite incr_closed; auto|].
    destruct n as [|n']; [auto|]. destruct (Nat.leb t n'); auto.
  - auto.
  - apply forallb_opt in Hs. apply forallb_forall. intros u Hin.
    apply in_map_iff in Hin as (u0 & <- & Hin0).
    rewrite Forall_forall in H, Hs. specialize (H u0 Hin0). specialize (Hs u0 Hin0).
    destruct H as [-> | (s & g & -> & Hg)]; auto.
    destruct Hs as [Hn | (s' & g' & He & Hg')]; [discriminate|].
    injection He as <- <-. auto.
  - destruct P as [k| |p q xs|g]; simpl in Hs; try discriminate.
    + reflexivity.
    + specialize (IHP (S t) (S m) G Hs HG Hc). simpl in IHP |- *. exact IHP.
Qed.

(* The one-step unfolding of a recursion. *)
Definition unf (P : global) : global :=
  match P with g_rec g => gsubst 0 0 (g_rec g) g | _ => P end.

(* Guardedness of every closed, shaped term, for every depth. *)
Lemma guard_list : forall n (xs : list (option (sort * global))),
  Forall (fun u => u = None \/ exists s g, u = Some (s, g) /\ exists m, guardG n m g) xs ->
  exists m, Forall (fun u => u = None \/ exists s g, u = Some (s, g) /\ guardG n m g) xs.
Proof.
  intros n xs H. induction H as [|u xs Hu Hxs IH].
  - exists 0. constructor.
  - destruct IH as [m1 Hm1]. destruct Hu as [-> | (s & g & -> & m2 & Hm2)].
    + exists m1. constructor; auto.
    + exists (Nat.max m1 m2). constructor.
      * right. exists s, g. split; auto. eapply guardG_more; [exact Hm2 | apply to_leq; lia].
      * eapply Forall_impl; [|exact Hm1]. intros u [-> | (s' & g' & -> & Hg')]; [left; auto|].
        right. exists s', g'. split; auto. eapply guardG_more; [exact Hg' | apply to_leq; lia].
Qed.

Lemma guard_closed : forall n P, cl 0 P = true -> shaped P = true -> exists m, guardG n m P.
Proof.
  induction n as [|n IH]; intros P Hc Hs.
  - exists 0. apply gg_nil.
  - (* first the terms that are not a recursion *)
    assert (Hnr : forall Q, cl 0 Q = true -> shaped Q = true ->
                  (forall g, Q <> g_rec g) -> exists m, guardG (S n) m Q).
    { intros Q HQc HQs HQr. destruct Q as [k| |p q xs|g].
      - simpl in HQc. apply Nat.ltb_lt in HQc. lia.
      - exists 0. apply gg_end.
      - simpl in HQc, HQs. apply forallb_opt in HQc. apply forallb_opt in HQs.
        destruct (guard_list n xs) as [m Hm].
        { rewrite Forall_forall in *. intros u Hin.
          specialize (HQc u Hin). specialize (HQs u Hin).
          destruct HQc as [-> | (s & g & -> & Hg)]; [left; auto|].
          destruct HQs as [Hn | (s' & g' & He & Hg')]; [discriminate|].
          injection He as <- <-. right. exists s, g. split; auto. }
        exists m. apply gg_send. exact Hm.
      - exfalso. eapply HQr. reflexivity. }
    destruct P as [k| |p q xs|g]; try (apply Hnr; auto; intros g' He; discriminate).
    set (Q := gsubst 0 0 (g_rec g) g).
    assert (HQc : cl 0 Q = true) by (apply gsubst_cl; auto).
    assert (HQs : shaped Q = true).
    { apply gsubst_shaped; auto. simpl in Hs. destruct g; try discriminate; simpl; auto. }
    assert (HQr : forall g', Q <> g_rec g').
    { intros g' He. unfold Q in He. simpl in Hs. destruct g; try discriminate; simpl in He; discriminate. }
    destruct (Hnr Q HQc HQs HQr) as [m Hm].
    exists (S m). eapply gg_rec; [apply gsubst_correct | exact Hm].
Qed.

Fixpoint geqb (a b : global) : bool :=
  match a, b with
  | g_var n, g_var m => Nat.eqb n m
  | g_end, g_end => true
  | g_send p q xs, g_send p' q' ys =>
      Nat.eqb p p' && Nat.eqb q q' &&
      (fix go (xs ys : list (option (sort * global))) : bool :=
         match xs, ys with
         | [], [] => true
         | None :: xs', None :: ys' => go xs' ys'
         | Some (s, g) :: xs', Some (s', g') :: ys' =>
             match s, s' with
             | sbool, sbool | sint, sint | snat, snat => geqb g g' && go xs' ys'
             | _, _ => false
             end
         | _, _ => false
         end) xs ys
  | g_rec g, g_rec g' => geqb g g'
  | _, _ => false
  end.

Lemma geqb_sound : forall a b, geqb a b = true -> a = b.
Proof.
  intro a. induction a using global_ind_ref; intros b Hb; destruct b; simpl in Hb; try discriminate.
  - apply Nat.eqb_eq in Hb. now subst.
  - reflexivity.
  - apply andb_prop in Hb as [Hb Hl]. apply andb_prop in Hb as [Hp Hq].
    apply Nat.eqb_eq in Hp, Hq. subst. f_equal.
    revert l Hl. induction H as [|u xs Hu Hxs IH]; intros os Hl.
    + destruct os; simpl in Hl; [reflexivity | discriminate].
    + destruct u as [[s g]|]; destruct os as [|[[s' g']|] os]; simpl in Hl; try discriminate.
      * destruct Hu as [Hn | (s0 & g0 & He & Hg)]; [discriminate|]. injection He as <- <-.
        destruct s, s'; try discriminate; apply andb_prop in Hl as [Hg' Hl];
          rewrite (Hg g' Hg'), (IH os Hl); reflexivity.
      * rewrite (IH os Hl). reflexivity.
  - f_equal. auto.
Qed.

(* ── Lists ─────────────────────────────────────────────────────────── *)

Lemma onth_map : forall A B (f : option A -> option B) n (l : list (option A)),
  f None = None -> onth n (map f l) = f (onth n l).
Proof.
  intros A B f n. induction n as [|n IH]; intros l Hf; destruct l as [|x l]; simpl; auto.
Qed.

Lemma onth_forallb : forall A (f : option A -> bool) n l x,
  forallb f l = true -> onth n l = Some x -> f (Some x) = true.
Proof.
  intros A f n. induction n as [|n IH]; intros l x Hf Hn; destruct l as [|y l]; simpl in *;
    try discriminate; apply andb_prop in Hf as [Hy Hl]; [now subst | eauto].
Qed.

Lemma onth_in : forall A n (l : list (option A)) x, onth n l = Some x -> In (Some x) l.
Proof.
  intros A n. induction n as [|n IH]; intros l x Hn; destruct l as [|y l]; simpl in *;
    try discriminate; [left; auto | right; eauto].
Qed.

Lemma existsb_some : forall A (l : list (option A)),
  existsb (fun u => match u with Some _ => true | None => false end) l = true ->
  exists n x, onth n l = Some x.
Proof.
  intros A l. induction l as [|u l IH]; simpl; [discriminate|]. intro H.
  destruct u as [x|]; [exists 0, x; reflexivity|].
  destruct (IH H) as (n & x & Hn). exists (S n), x. exact Hn.
Qed.

Lemma forallb_seq : forall f n i, forallb f (seq 0 n) = true -> i < n -> f i = true.
Proof.
  intros f n i Hf Hi. apply forallb_forall with (x := i) in Hf; auto. apply in_seq. lia.
Qed.

(* ── Data ──────────────────────────────────────────────────────────── *)

(* A global state: its head (None for end, Some (p, q) for a message from p to q) and its
   children as indices of states.  A local state: kind 0 (end), 1 (send) or 2 (receive),
   the peer, and its children as indices of local states. *)
Record gstate := GS { gs_head : option (nat * nat); gs_kids : list (option (sort * nat)) }.
Record lstate := LS { ls_kind : nat; ls_peer : nat; ls_kids : list (option (sort * nat)) }.

Record data := D {
  d_terms : list global;
  d_g : list gstate;
  d_parts : list nat;
  d_rank : list (list (option nat));
  d_l : list lstate;
  d_proj : list (list nat);
  d_ctx : list (nat * nat);
  d_sub : list (nat * nat)
}.

Section Checks.
Variable d : data.

Definition n_g : nat := length (d_g d).
Definition n_l : nat := length (d_l d).
Definition gst (i : nat) : gstate := nth i (d_g d) (GS None []).
Definition term (i : nat) : global := nth i (d_terms d) g_end.
Definition lst (j : nat) : lstate := nth j (d_l d) (LS 0 0 []).
Definition part_at (k : nat) : nat := nth k (d_parts d) 0.
Definition rank_of (k i : nat) : option nat := nth i (nth k (d_rank d) []) None.
Definition proj_of (k i : nat) : nat := nth i (nth k (d_proj d) []) 0.

Definition sort_eqb (a b : sort) : bool :=
  match a, b with sbool, sbool | sint, sint | snat, snat => true | _, _ => false end.

Lemma sort_eqb_sound : forall a b, sort_eqb a b = true -> a = b.
Proof. intros [] []; simpl; congruence. Qed.

Definition kids_in (n : nat) (ks : list (option (sort * nat))) : bool :=
  forallb (fun u => match u with Some (_, j) => Nat.ltb j n | None => true end) ks.

Definition has_some {A} (ks : list (option A)) : bool :=
  existsb (fun u => match u with Some _ => true | None => false end) ks.

Fixpoint kids_match (xs : list (option (sort * global))) (ks : list (option (sort * nat))) : bool :=
  match xs, ks with
  | [], [] => true
  | None :: xs', None :: ks' => kids_match xs' ks'
  | Some (s, g) :: xs', Some (s', j) :: ks' => sort_eqb s s' && geqb g (term j) && kids_match xs' ks'
  | _, _ => false
  end.

Definition state_ok (i : nat) : bool :=
  let P := term i in
  cl 0 P && shaped P && wfb P && kids_in n_g (gs_kids (gst i)) &&
  match unf P, gs_head (gst i) with
  | g_end, None => match gs_kids (gst i) with [] => true | _ => false end
  | g_send p q xs, Some (p', q') =>
      Nat.eqb p p' && Nat.eqb q q' && has_some (gs_kids (gst i)) && kids_match xs (gs_kids (gst i))
  | _, _ => false
  end.

Definition gnode_at (i : nat) : gnode :=
  match gs_head (gst i) with None => gnode_end | Some (p, q) => gnode_pq p q end.

Definition inv (p : nat) (gn : gnode) : bool :=
  match gn with gnode_pq a b => Nat.eqb a p || Nat.eqb b p | _ => false end.

Definition rank_ok (k i : nat) : bool :=
  let p := part_at k in
  match rank_of k i with
  | None =>
      negb (inv p (gnode_at i)) &&
      forallb (fun u => match u with
                        | Some (_, j) => match rank_of k j with None => true | Some _ => false end
                        | None => true end) (gs_kids (gst i))
  | Some r =>
      inv p (gnode_at i) ||
      (Nat.ltb 0 r && match gs_head (gst i) with Some _ => true | None => false end &&
       forallb (fun u => match u with
                         | Some (_, j) => match rank_of k j with Some r' => Nat.ltb r' r | None => false end
                         | None => true end) (gs_kids (gst i)))
  end.

Definition in_parts (p : nat) : bool := existsb (Nat.eqb p) (d_parts d).

Definition node_parts_ok (i : nat) : bool :=
  match gs_head (gst i) with Some (a, b) => in_parts a && in_parts b | None => true end.

Fixpoint nodupb (l : list nat) : bool :=
  match l with [] => true | x :: l' => negb (existsb (Nat.eqb x) l') && nodupb l' end.

Definition global_ok : bool :=
  Nat.ltb 0 n_g && Nat.eqb (length (d_terms d)) n_g &&
  forallb state_ok (seq 0 n_g) && forallb node_parts_ok (seq 0 n_g) &&
  nodupb (d_parts d) &&
  forallb (fun k => forallb (rank_ok k) (seq 0 n_g) &&
                    match rank_of k 0 with Some _ => true | None => false end)
          (seq 0 (length (d_parts d))).

(* A local state: its children are in range, and an action has a last branch. *)
Definition lstate_ok (j : nat) : bool :=
  kids_in n_l (ls_kids (lst j)) &&
  match ls_kind (lst j) with
  | 0 => true
  | 1 | 2 => slistb (ls_kids (lst j))
  | _ => false
  end.

(* The children of a global node and of its projection correspond branch by branch. *)
Fixpoint kids_proj (k : nat) (gks lks : list (option (sort * nat))) : bool :=
  match gks, lks with
  | [], [] => true
  | None :: gks', None :: lks' => kids_proj k gks' lks'
  | Some (s, g) :: gks', Some (s', j) :: lks' =>
      sort_eqb s s' && Nat.eqb (proj_of k g) j && kids_proj k gks' lks'
  | _, _ => false
  end.

(* The plain merge: every branch projects to local state j, and the last branch is present. *)
Fixpoint merge_ok (k j : nat) (gks : list (option (sort * nat))) : bool :=
  match gks with
  | [] => false
  | [Some (_, g)] => Nat.eqb (proj_of k g) j
  | None :: gks' => merge_ok k j gks'
  | Some (_, g) :: gks' => Nat.eqb (proj_of k g) j && merge_ok k j gks'
  end.

Definition is_kind (j kind peer : nat) : bool :=
  Nat.eqb (ls_kind (lst j)) kind && Nat.eqb (ls_peer (lst j)) peer.

(* The projection rules of the development, one per shape of the global node. *)
Definition proj_ok (k i : nat) : bool :=
  let r := part_at k in
  let j := proj_of k i in
  Nat.ltb j n_l &&
  match rank_of k i with
  | None => Nat.eqb (ls_kind (lst j)) 0
  | Some _ =>
      match gs_head (gst i) with
      | None => false
      | Some (p, q) =>
          if Nat.eqb p r then
            negb (Nat.eqb q r) && is_kind j 1 q && kids_proj k (gs_kids (gst i)) (ls_kids (lst j))
          else if Nat.eqb q r then
            is_kind j 2 p && kids_proj k (gs_kids (gst i)) (ls_kids (lst j))
          else negb (Nat.eqb p q) && merge_ok k j (gs_kids (gst i))
      end
  end.

Definition subsortb (a b : sort) : bool :=
  match a, b with snat, sint => true | _, _ => sort_eqb a b end.

Lemma subsortb_sound : forall a b, subsortb a b = true -> subsort a b.
Proof. intros [] []; simpl; try discriminate; intros _; constructor. Qed.

Fixpoint memb (a b : nat) (l : list (nat * nat)) : bool :=
  match l with
  | [] => false
  | (x, y) :: l' => (Nat.eqb x a && Nat.eqb y b) || memb a b l'
  end.

Lemma memb_in : forall a b l, memb a b l = true -> In (a, b) l.
Proof.
  intros a b l. induction l as [|[x y] l IH]; simpl; [discriminate|]. intro H.
  apply orb_prop in H as [H | H]; [left | right; auto].
  apply andb_prop in H as [Hx Hy]. apply Nat.eqb_eq in Hx, Hy. now subst.
Qed.

(* wfsend over the list of the subtype and the list of the supertype. *)
Fixpoint sendb (xs ys : list (option (sort * nat))) : bool :=
  match xs, ys with
  | None :: xs', None :: ys' => sendb xs' ys'
  | Some (s, t) :: xs', Some (s', t') :: ys' => subsortb s s' && memb t t' (d_sub d) && sendb xs' ys'
  | None :: xs', Some _ :: ys' => sendb xs' ys'
  | [], _ => true
  | _, _ => false
  end.

(* wfrec over the list of the supertype and the list of the subtype. *)
Fixpoint recvb (ys xs : list (option (sort * nat))) : bool :=
  match ys, xs with
  | None :: ys', None :: xs' => recvb ys' xs'
  | Some (s', t') :: ys', Some (s, t) :: xs' => subsortb s' s && memb t t' (d_sub d) && recvb ys' xs'
  | None :: ys', Some _ :: xs' => recvb ys' xs'
  | [], _ => true
  | _, _ => false
  end.

(* One step of the subtyping rules of the development for the pair (a, b). *)
Definition sub_ok (a b : nat) : bool :=
  Nat.ltb a n_l && Nat.ltb b n_l &&
  match ls_kind (lst a), ls_kind (lst b) with
  | 0, 0 => true
  | 1, 1 => Nat.eqb (ls_peer (lst a)) (ls_peer (lst b)) && sendb (ls_kids (lst a)) (ls_kids (lst b))
  | 2, 2 => Nat.eqb (ls_peer (lst a)) (ls_peer (lst b)) && recvb (ls_kids (lst b)) (ls_kids (lst a))
  | _, _ => false
  end.

Fixpoint index_of (p : nat) (l : list nat) : option nat :=
  match l with
  | [] => None
  | x :: l' => if Nat.eqb p x then Some 0 else option_map S (index_of p l')
  end.

(* A context entry of a participant is a subtype of its projection at the root.  Any other
   entry is an end. *)
Definition ctx_entry_ok (e : nat * nat) : bool :=
  let (p, j) := e in
  Nat.ltb j n_l &&
  match index_of p (d_parts d) with
  | Some k => memb j (proj_of k 0) (d_sub d)
  | None => Nat.eqb (ls_kind (lst j)) 0
  end.

Definition local_ok : bool :=
  Nat.ltb 0 n_l && forallb lstate_ok (seq 0 n_l) &&
  forallb (fun k => forallb (proj_ok k) (seq 0 n_g)) (seq 0 (length (d_parts d))) &&
  forallb (fun ab => sub_ok (fst ab) (snd ab)) (d_sub d) &&
  forallb ctx_entry_ok (d_ctx d) &&
  forallb (fun k => existsb (fun e => Nat.eqb (fst e) (part_at k)) (d_ctx d))
          (seq 0 (length (d_parts d))).

End Checks.

(* ── The global tree ───────────────────────────────────────────────── *)

Section Global.
Variable d : data.
Variable tree : nat -> gtt.

Definition gkid (u : option (sort * nat)) : option (sort * gtt) :=
  match u with Some (s, j) => Some (s, tree j) | None => None end.

Definition gshape (i : nat) : gtt :=
  match gs_head (gst d i) with
  | None => gtt_end
  | Some (p, q) => gtt_send p q (map gkid (gs_kids (gst d i)))
  end.

Hypothesis tree_unf : forall i, i < n_g d -> tree i = gshape i.
Hypothesis Hglobal : global_ok d = true.

Lemma Hg_parts : forall i, i < n_g d -> state_ok d i = true /\ node_parts_ok d i = true.
Proof.
  intros i Hi. unfold global_ok in Hglobal.
  repeat match goal with H : _ && _ = true |- _ => apply andb_prop in H as [? ?] end.
  split; eapply forallb_seq; eauto.
Qed.

Lemma Hg_rank : forall k i, k < length (d_parts d) -> i < n_g d -> rank_ok d k i = true.
Proof.
  intros k i Hk Hi. unfold global_ok in Hglobal.
  repeat match goal with H : _ && _ = true |- _ => apply andb_prop in H as [? ?] end.
  pose proof (forallb_seq _ _ _ H0 Hk) as Hr. apply andb_prop in Hr as [Hr _].
  eapply forallb_seq; eauto.
Qed.

Lemma Hg_root : forall k, k < length (d_parts d) -> exists r, rank_of d k 0 = Some r.
Proof.
  intros k Hk. unfold global_ok in Hglobal.
  repeat match goal with H : _ && _ = true |- _ => apply andb_prop in H as [? ?] end.
  pose proof (forallb_seq _ _ _ H0 Hk) as Hr. apply andb_prop in Hr as [_ Hr].
  destruct (rank_of d k 0); [eauto | discriminate].
Qed.

Lemma Hg_nodup : nodupb (d_parts d) = true.
Proof.
  unfold global_ok in Hglobal.
  repeat match goal with H : _ && _ = true |- _ => apply andb_prop in H as [? ?] end. auto.
Qed.

Lemma Hg_nonempty : 0 < n_g d.
Proof.
  unfold global_ok in Hglobal.
  repeat match goal with H : _ && _ = true |- _ => apply andb_prop in H as [? ?] end.
  now apply Nat.ltb_lt.
Qed.

Lemma kids_match_forall2 : forall xs ks, kids_match d xs ks = true -> kids_in (n_g d) ks = true ->
  Forall2 (fun u v => (u = None /\ v = None) \/
                      exists s j, u = Some (s, term d j) /\ v = Some (s, j) /\ j < n_g d) xs ks.
Proof.
  induction xs as [|u xs IH]; intros ks Hm Hin.
  - destruct ks; simpl in Hm; [constructor | discriminate].
  - destruct u as [[s g]|].
    + destruct ks as [|[[s' j]|] ks]; simpl in Hm; try discriminate.
      unfold kids_in in Hin; simpl in Hin.
      apply andb_prop in Hin as [Hv Hin]. apply andb_prop in Hm as [Hm Hks].
      apply andb_prop in Hm as [Hs Hg]. apply sort_eqb_sound in Hs. apply geqb_sound in Hg. subst.
      apply Nat.ltb_lt in Hv. constructor; [| apply IH; auto].
      right. do 2 eexists. split; [reflexivity | split; [reflexivity | exact Hv]].
    + destruct ks as [|[[s' j]|] ks]; simpl in Hm; try discriminate.
      unfold kids_in in Hin; simpl in Hin.
      constructor; [left; split; reflexivity | apply IH; auto].
Qed.

Lemma state_view : forall i, i < n_g d ->
  cl 0 (term d i) = true /\ shaped (term d i) = true /\ wfb (term d i) = true /\
  kids_in (n_g d) (gs_kids (gst d i)) = true /\
  ((unf (term d i) = g_end /\ gs_head (gst d i) = None /\ gs_kids (gst d i) = []) \/
   (exists p q xs, unf (term d i) = g_send p q xs /\ gs_head (gst d i) = Some (p, q) /\
      has_some (gs_kids (gst d i)) = true /\
      Forall2 (fun u v => (u = None /\ v = None) \/
                          exists s j, u = Some (s, term d j) /\ v = Some (s, j) /\ j < n_g d)
              xs (gs_kids (gst d i)))).
Proof.
  intros i Hi. destruct (Hg_parts i Hi) as [Hs _]. unfold state_ok in Hs.
  repeat match goal with H : _ && _ = true |- _ => apply andb_prop in H as [? ?] end.
  repeat split; auto.
  destruct (unf (term d i)) as [k| |p q xs|g] eqn:Hu; destruct (gs_head (gst d i)) as [[p' q']|] eqn:Hh;
    try discriminate.
  - left. destruct (gs_kids (gst d i)) eqn:Hk; [auto | discriminate].
  - right. repeat match goal with H : _ && _ = true |- _ => apply andb_prop in H as [? ?] end.
    repeat match goal with H : Nat.eqb _ _ = true |- _ => apply Nat.eqb_eq in H; subst end.
    exists p', q', xs. repeat split; auto.
    now apply kids_match_forall2.
Qed.

Lemma tree_view : forall i, i < n_g d ->
  (gs_head (gst d i) = None /\ tree i = gtt_end) \/
  (exists p q, gs_head (gst d i) = Some (p, q) /\ tree i = gtt_send p q (map gkid (gs_kids (gst d i)))).
Proof.
  intros i Hi. rewrite (tree_unf i Hi). unfold gshape.
  destruct (gs_head (gst d i)) as [[p q]|]; [right; exists p, q | left]; auto.
Qed.

(* The recursion term of each state unfolds to its tree: the witness of wfgC. *)
Lemma gttTC_states : forall i, i < n_g d -> gttTC (term d i) (tree i).
Proof.
  assert (H : forall P T, (exists i, i < n_g d /\ T = tree i /\ (P = term d i \/ P = unf (term d i))) ->
                          paco2 gttT bot2 P T).
  { pcofix CIH. intros P T (i & Hi & -> & HP). pfold.
    destruct (state_view i Hi) as (Hc & Hs & Hw & Hk & Hv).
    assert (Hunf : P = unf (term d i) \/ exists g, P = g_rec g /\ unf P = unf (term d i)).
    { destruct HP as [-> | ->]; [|left; auto].
      destruct (term d i) as [k| |p q xs|g] eqn:Ht; simpl; auto. right. exists g. auto. }
    destruct Hunf as [HP' | (g & -> & Hug)].
    - rewrite HP'. rewrite (tree_unf i Hi). unfold gshape.
      destruct Hv as [(-> & -> & _) | (p & q & xs & -> & -> & _ & Hf)]; [apply gttT_end|].
      apply gttT_send. clear -Hf CIH.
      induction Hf as [|u v xs ks Huv Hf IH]; constructor; auto.
      destruct Huv as [(-> & ->) | (s & j & -> & -> & Hj)]; [left; auto|].
      right. exists s, (term d j), (tree j). repeat split; auto.
      right. apply CIH. exists j. auto.
    - eapply gttT_rec; [apply gsubst_correct|].
      right. apply CIH. exists i. split; [exact Hi | split; [reflexivity | right; exact Hug]]. }
  intros i Hi. apply H. exists i. auto.
Qed.

Lemma wfgCw_states : forall i, i < n_g d -> wfgCw (tree i).
Proof.
  intros i Hi. destruct (state_view i Hi) as (Hc & Hs & Hw & _).
  exists (term d i). repeat split.
  - now apply gttTC_states.
  - now apply wfb_sound.
  - intro n. now apply guard_closed.
Qed.

(* ── Paths ── *)

Fixpoint gpath (i : nat) (w : list nat) : option nat :=
  match w with
  | [] => Some i
  | n :: w' => match onth n (gs_kids (gst d i)) with Some (_, j) => gpath j w' | None => None end
  end.

Lemma gpath_app : forall w1 w2 i, gpath i (w1 ++ w2) =
  match gpath i w1 with Some j => gpath j w2 | None => None end.
Proof.
  induction w1 as [|n w1 IH]; intros w2 i; simpl; auto.
  destruct (onth n (gs_kids (gst d i))) as [[s j]|]; auto.
Qed.

Lemma kid_bound : forall i n s j, i < n_g d -> onth n (gs_kids (gst d i)) = Some (s, j) -> j < n_g d.
Proof.
  intros i n s j Hi Hn. destruct (state_view i Hi) as (_ & _ & _ & Hk & _).
  unfold kids_in in Hk. pose proof (onth_forallb _ _ _ _ _ Hk Hn) as H. simpl in H.
  now apply Nat.ltb_lt.
Qed.

Lemma gpath_bound : forall w i j, i < n_g d -> gpath i w = Some j -> j < n_g d.
Proof.
  induction w as [|n w IH]; intros i j Hi Hp; simpl in Hp.
  - injection Hp as <-. auto.
  - destruct (onth n (gs_kids (gst d i))) as [[s j1]|] eqn:Hn; [|discriminate].
    eapply IH; [eapply kid_bound|]; eauto.
Qed.

Lemma gttmap_path : forall w i gn, i < n_g d -> gttmap (tree i) w None gn ->
  exists j, gpath i w = Some j /\ gn = gnode_at d j.
Proof.
  induction w as [|n w IH]; intros i gn Hi Hm.
  - exists i. split; auto. unfold gnode_at.
    destruct (tree_view i Hi) as [(Hh & Ht) | (p & q & Hh & Ht)]; rewrite Ht in Hm; rewrite Hh;
      inversion Hm; auto.
  - destruct (tree_view i Hi) as [(Hh & Ht) | (p & q & Hh & Ht)]; rewrite Ht in Hm; inversion Hm; subst.
    match goal with H : onth _ _ = Some _ |- _ => rename H into Hon end.
    rewrite onth_map in Hon; [|reflexivity].
    destruct (onth n (gs_kids (gst d i))) as [[s j]|] eqn:Hn; simpl in Hon; try discriminate.
    injection Hon as <- <-. simpl. rewrite Hn.
    eapply IH; [eapply kid_bound|]; eauto.
Qed.

Lemma path_gttmap : forall w i j, i < n_g d -> gpath i w = Some j -> gttmap (tree i) w None (gnode_at d j).
Proof.
  induction w as [|n w IH]; intros i j Hi Hp; simpl in Hp.
  - injection Hp as <-. unfold gnode_at.
    destruct (tree_view i Hi) as [(Hh & Ht) | (p & q & Hh & Ht)]; rewrite Ht, Hh; constructor.
  - destruct (onth n (gs_kids (gst d i))) as [[s j1]|] eqn:Hn; [|discriminate].
    destruct (tree_view i Hi) as [(Hh & Ht) | (p & q & Hh & Ht)].
    + destruct (state_view i Hi) as (_ & _ & _ & _ & [(_ & _ & Hk) | (p & q & xs & _ & Hh' & _)]).
      * rewrite Hk in Hn. destruct n; discriminate.
      * congruence.
    + rewrite Ht. eapply gmap_con with (st := s) (gk := tree j1).
      * rewrite onth_map; [|reflexivity]. rewrite Hn. reflexivity.
      * eapply IH; [eapply kid_bound|]; eauto.
Qed.

(* ── Balance ── *)

Lemma rank_none : forall k w i j, k < length (d_parts d) -> i < n_g d -> rank_of d k i = None ->
  gpath i w = Some j -> inv (part_at d k) (gnode_at d j) = false.
Proof.
  intros k w. induction w as [|n w IH]; intros i j Hk Hi Hr Hp; simpl in Hp;
    pose proof (Hg_rank k i Hk Hi) as Hok; unfold rank_ok in Hok; rewrite Hr in Hok;
    apply andb_prop in Hok as [Hn Hkids].
  - injection Hp as <-. now apply negb_true_iff in Hn.
  - destruct (onth n (gs_kids (gst d i))) as [[s j1]|] eqn:Hon; [|discriminate].
    pose proof (onth_forallb _ _ _ _ _ Hkids Hon) as Hj1. simpl in Hj1.
    destruct (rank_of d k j1) eqn:Hr1; [discriminate|].
    exact (IH j1 j Hk (kid_bound i n s j1 Hi Hon) Hr1 Hp).
Qed.

Lemma rank_some : forall k w r i j, k < length (d_parts d) -> i < n_g d -> rank_of d k i = Some r ->
  gpath i w = Some j -> (gnode_at d j = gnode_end \/ r <= length w) ->
  exists w2 w0 j2, w = w2 ++ w0 /\ gpath i w2 = Some j2 /\ inv (part_at d k) (gnode_at d j2) = true.
Proof.
  intros k w. induction w as [|n w IH]; intros r i j Hk Hi Hr Hp Hend;
    pose proof (Hg_rank k i Hk Hi) as Hok; unfold rank_ok in Hok; rewrite Hr in Hok;
    (destruct (inv (part_at d k) (gnode_at d i)) eqn:Hinv;
     [exists [], (_ :: _) , i; simpl; auto; fail
     |idtac]) || idtac.
  - simpl in Hp. injection Hp as <-.
    destruct (inv (part_at d k) (gnode_at d i)) eqn:Hinv; [exists [], [], i; auto|].
    simpl in Hok. apply andb_prop in Hok as [Hok _]. apply andb_prop in Hok as [H0 Hh].
    apply Nat.ltb_lt in H0. simpl in Hend.
    destruct Hend as [He | He]; [|lia]. unfold gnode_at in He.
    destruct (gs_head (gst d i)) as [[a b]|]; discriminate.
  - destruct (inv (part_at d k) (gnode_at d i)) eqn:Hinv; [exists [], (n :: w), i; auto|].
    simpl in Hok. apply andb_prop in Hok as [Hok Hkids]. apply andb_prop in Hok as [H0 _].
    apply Nat.ltb_lt in H0. simpl in Hp.
    destruct (onth n (gs_kids (gst d i))) as [[s j1]|] eqn:Hon; [|discriminate].
    pose proof (onth_forallb _ _ _ _ _ Hkids Hon) as Hj1. simpl in Hj1.
    destruct (rank_of d k j1) as [r1|] eqn:Hr1; [|discriminate]. apply Nat.ltb_lt in Hj1.
    assert (Hj1n : j1 < n_g d) by (eapply kid_bound; eauto).
    destruct (IH r1 j1 j Hk Hj1n Hr1 Hp) as (w2 & w0 & j2 & -> & Hp2 & Hinv2).
    { destruct Hend as [He | He]; [left; auto | right; simpl in He; lia]. }
    exists (n :: w2), w0, j2. simpl. rewrite Hon. auto.
Qed.

Lemma rank_path : forall k r i, k < length (d_parts d) -> i < n_g d -> rank_of d k i = Some r ->
  exists w j, gpath i w = Some j /\ inv (part_at d k) (gnode_at d j) = true.
Proof.
  intros k r. induction r as [r IH] using (well_founded_induction lt_wf). intros i Hk Hi Hr.
  pose proof (Hg_rank k i Hk Hi) as Hok. unfold rank_ok in Hok. rewrite Hr in Hok.
  destruct (inv (part_at d k) (gnode_at d i)) eqn:Hinv; [exists [], i; auto|].
  simpl in Hok. apply andb_prop in Hok as [Hok Hkids]. apply andb_prop in Hok as [H0 Hh].
  destruct (state_view i Hi) as (_ & _ & _ & _ & [(_ & Hh' & _) | (p & q & xs & _ & Hh' & Hsome & _)]).
  - rewrite Hh' in Hh. discriminate.
  - apply existsb_some in Hsome as (n & [s j1] & Hon).
    pose proof (onth_forallb _ _ _ _ _ Hkids Hon) as Hj1. simpl in Hj1.
    destruct (rank_of d k j1) as [r1|] eqn:Hr1; [|discriminate]. apply Nat.ltb_lt in Hj1.
    assert (Hj1n : j1 < n_g d) by (eapply kid_bound; eauto).
    destruct (IH r1 Hj1 j1 Hk Hj1n Hr1) as (w & j & Hp & Hinvj).
    exists (n :: w), j. simpl. rewrite Hon. auto.
Qed.

Lemma in_parts_index : forall p, in_parts d p = true -> exists k, k < length (d_parts d) /\ part_at d k = p.
Proof.
  intros p Hp. unfold in_parts, part_at in *. induction (d_parts d) as [|x l IH]; simpl in Hp; [discriminate|].
  apply orb_prop in Hp as [Hx | Hl].
  - apply Nat.eqb_eq in Hx. exists 0. simpl. split; [lia | auto].
  - destruct (IH Hl) as (k & Hk & Hn). exists (S k). simpl. split; [lia | auto].
Qed.

Lemma inv_parts : forall j p, j < n_g d -> inv p (gnode_at d j) = true -> in_parts d p = true.
Proof.
  intros j p Hj Hinv. destruct (Hg_parts j Hj) as [_ Hn]. unfold node_parts_ok in Hn.
  unfold gnode_at, inv in Hinv. destruct (gs_head (gst d j)) as [[a b]|]; [|discriminate].
  apply andb_prop in Hn as [Ha Hb]. apply orb_prop in Hinv as [He | He]; apply Nat.eqb_eq in He; subst; auto.
Qed.

Lemma balanced_states : forall i, i < n_g d -> balancedG (tree i).
Proof.
  intros i Hi. unfold balancedG. intros w w' p q gn Hw Hpq.
  destruct (gttmap_path w i gn Hi Hw) as (j0 & Hp0 & ->).
  assert (Hj0 : j0 < n_g d) by (eapply gpath_bound; eauto).
  assert (Hpart : exists j1, gpath j0 w' = Some j1 /\ inv p (gnode_at d j1) = true).
  { destruct Hpq as [Hm | Hm]; destruct (gttmap_path _ _ _ Hi Hm) as (j1 & Hp1 & Hn1);
      rewrite gpath_app, Hp0 in Hp1; exists j1; split; auto; rewrite <- Hn1; simpl;
      rewrite Nat.eqb_refl; [auto | apply orb_true_r]. }
  destruct Hpart as (j1 & Hp1 & Hinv1).
  assert (Hj1 : j1 < n_g d) by (eapply gpath_bound; eauto).
  destruct (in_parts_index p (inv_parts j1 p Hj1 Hinv1)) as (k & Hk & <-).
  destruct (rank_of d k j0) as [r|] eqn:Hr.
  2:{ exfalso. rewrite (rank_none k w' j0 j1 Hk Hj0 Hr Hp1) in Hinv1. discriminate. }
  exists r. intros w'' Hw''.
  assert (Hend : exists j, gpath j0 w'' = Some j /\ (gnode_at d j = gnode_end \/ r <= length w'')).
  { destruct Hw'' as [Hm | (Hl & tc & Hm)].
    - destruct (gttmap_path _ _ _ Hi Hm) as (j & Hpj & Hnj). rewrite gpath_app, Hp0 in Hpj.
      exists j. split; [exact Hpj | left; symmetry; exact Hnj].
    - destruct (gttmap_path _ _ _ Hi Hm) as (j & Hpj & Hnj). rewrite gpath_app, Hp0 in Hpj.
      exists j. split; [exact Hpj | right; lia]. }
  destruct Hend as (j & Hpj & Hend).
  destruct (rank_some k w'' r j0 j Hk Hj0 Hr Hpj Hend) as (w2 & w0 & j2 & -> & Hp2 & Hinv2).
  exists w2, w0. split; auto.
  assert (Hm2 : gttmap (tree i) (w ++ w2) None (gnode_at d j2)).
  { apply path_gttmap; auto. rewrite gpath_app, Hp0. exact Hp2. }
  unfold inv in Hinv2. destruct (gnode_at d j2) as [|a b|] eqn:Hn2; try discriminate.
  apply orb_prop in Hinv2 as [He | He]; apply Nat.eqb_eq in He; subst.
  - exists b. left. exact Hm2.
  - exists a. right. exact Hm2.
Qed.

Lemma wfgC_states : forall i, i < n_g d -> wfgC (tree i).
Proof.
  intros i Hi. destruct (wfgCw_states i Hi) as (G' & H1 & H2 & H3).
  exists G'. split; [exact H1 | split; [exact H2 | split; [exact H3 | exact (balanced_states i Hi)]]].
Qed.

(* ── Participation ── *)

Lemma part_pos : forall k r i, k < length (d_parts d) -> i < n_g d -> rank_of d k i = Some r ->
  isgPartsC (part_at d k) (tree i).
Proof.
  intros k r i Hk Hi Hr. destruct (rank_path k r i Hk Hi Hr) as (w & j & Hp & Hinv).
  pose proof (path_gttmap w i j Hi Hp) as Hm.
  unfold inv in Hinv. destruct (gnode_at d j) as [|a b|] eqn:Hn; try discriminate.
  apply orb_prop in Hinv as [He | He]; apply Nat.eqb_eq in He; subst.
  - eapply word_to_parts with (q0 := b); [left; exact Hm | now apply wfgCw_states].
  - eapply word_to_parts with (q0 := a); [right; exact Hm | now apply wfgCw_states].
Qed.

Lemma part_neg : forall k i, k < length (d_parts d) -> i < n_g d -> rank_of d k i = None ->
  ~ isgPartsC (part_at d k) (tree i).
Proof.
  intros k i Hk Hi Hr Hc. apply parts_to_word in Hc as (w & r & Hm).
  assert (H : exists j, gpath i w = Some j /\ inv (part_at d k) (gnode_at d j) = true).
  { destruct Hm as [Hm | Hm]; destruct (gttmap_path w i _ Hi Hm) as (j & Hp & Hn); exists j;
      split; auto; rewrite <- Hn; simpl; rewrite Nat.eqb_refl; [apply orb_true_r | auto]. }
  destruct H as (j & Hp & Hinv). rewrite (rank_none k w i j Hk Hi Hr Hp) in Hinv. discriminate.
Qed.

Lemma part_out : forall p i, in_parts d p = false -> i < n_g d -> ~ isgPartsC p (tree i).
Proof.
  intros p i Hp Hi Hc. apply parts_to_word in Hc as (w & r & Hm).
  assert (H : exists j, gpath i w = Some j /\ j < n_g d /\ inv p (gnode_at d j) = true).
  { destruct Hm as [Hm | Hm]; destruct (gttmap_path w i _ Hi Hm) as (j & Hpj & Hn); exists j;
      (split; [auto | split; [eapply gpath_bound; eauto|]]); rewrite <- Hn; simpl;
      rewrite Nat.eqb_refl; [apply orb_true_r | auto]. }
  destruct H as (j & _ & Hj & Hinv). rewrite (inv_parts j p Hj Hinv) in Hp. discriminate.
Qed.

(* ── The local trees ──────────────────────────────────────────────── *)

Variable ltree : nat -> ltt.

Definition lkid (u : option (sort * nat)) : option (sort * ltt) :=
  match u with Some (s, j) => Some (s, ltree j) | None => None end.

Definition lshape (j : nat) : ltt :=
  match ls_kind (lst d j) with
  | 0 => ltt_end
  | 1 => ltt_send (ls_peer (lst d j)) (map lkid (ls_kids (lst d j)))
  | _ => ltt_recv (ls_peer (lst d j)) (map lkid (ls_kids (lst d j)))
  end.

Hypothesis ltree_unf : forall j, j < n_l d -> ltree j = lshape j.
Hypothesis Hlocal : local_ok d = true.

Ltac local_parts :=
  let H := fresh in pose proof Hlocal as H; unfold local_ok in H;
  repeat match goal with Hc : _ && _ = true |- _ => apply andb_prop in Hc as [? ?] end.

Lemma Hl_lstate : forall j, j < n_l d -> lstate_ok d j = true.
Proof.
  intros j Hj. local_parts.
  match goal with Hc : forallb (lstate_ok d) _ = true |- _ => exact (forallb_seq _ _ _ Hc Hj) end.
Qed.

Lemma Hl_proj : forall k i, k < length (d_parts d) -> i < n_g d -> proj_ok d k i = true.
Proof.
  intros k i Hk Hi. local_parts.
  match goal with Hc : forallb (fun k => forallb (proj_ok d k) _) _ = true |- _ =>
    pose proof (forallb_seq _ _ _ Hc Hk) as Hr; cbv beta in Hr; exact (forallb_seq _ _ _ Hr Hi) end.
Qed.

Lemma Hl_sub : forall a b, In (a, b) (d_sub d) -> sub_ok d a b = true.
Proof.
  intros a b Hin. local_parts.
  match goal with Hc : forallb (fun ab => sub_ok d (fst ab) (snd ab)) _ = true |- _ =>
    exact (proj1 (forallb_forall _ _) Hc (a, b) Hin) end.
Qed.

Lemma Hl_entry : forall e, In e (d_ctx d) -> ctx_entry_ok d e = true.
Proof.
  intros e Hin. local_parts.
  match goal with Hc : forallb (ctx_entry_ok d) _ = true |- _ =>
    exact (proj1 (forallb_forall _ _) Hc e Hin) end.
Qed.

Lemma Hl_cover : forall k, k < length (d_parts d) ->
  existsb (fun e => Nat.eqb (fst e) (part_at d k)) (d_ctx d) = true.
Proof.
  intros k Hk. local_parts.
  match goal with Hc : forallb (fun k => existsb _ (d_ctx d)) _ = true |- _ =>
    exact (forallb_seq _ _ _ Hc Hk) end.
Qed.

Lemma ltree_view : forall j, j < n_l d ->
  kids_in (n_l d) (ls_kids (lst d j)) = true /\
  ((ls_kind (lst d j) = 0 /\ ltree j = ltt_end) \/
   (ls_kind (lst d j) = 1 /\ slistb (ls_kids (lst d j)) = true /\
    ltree j = ltt_send (ls_peer (lst d j)) (map lkid (ls_kids (lst d j)))) \/
   (ls_kind (lst d j) = 2 /\ slistb (ls_kids (lst d j)) = true /\
    ltree j = ltt_recv (ls_peer (lst d j)) (map lkid (ls_kids (lst d j))))).
Proof.
  intros j Hj. pose proof (Hl_lstate j Hj) as Hok. unfold lstate_ok in Hok.
  apply andb_prop in Hok as [Hin Hk]. split; [exact Hin|].
  rewrite (ltree_unf j Hj). unfold lshape.
  destruct (ls_kind (lst d j)) as [|[|[|n]]]; try discriminate; auto.
Qed.

Lemma slist_lkid : forall ks, slistb ks = true -> SList (map lkid ks).
Proof.
  intros ks H. apply slistb_sound. induction ks as [|u ks IH]; [discriminate|].
  destruct ks as [|u' ks].
  - destruct u as [[s j]|]; [reflexivity | discriminate].
  - change (slistb (lkid u :: map lkid (u' :: ks)) = true).
    destruct u as [[s j]|]; exact (IH H).
Qed.

Lemma lkids_forall : forall (P : ltt -> Prop) ks, kids_in (n_l d) ks = true ->
  (forall j, j < n_l d -> P (ltree j)) ->
  Forall (fun u => u = None \/ exists s g, u = Some (s, g) /\ P g) (map lkid ks).
Proof.
  intros P ks Hin HP. induction ks as [|u ks IH]; simpl; constructor;
    unfold kids_in in Hin; simpl in Hin.
  - destruct u as [[s j]|]; [|left; reflexivity].
    apply andb_prop in Hin as [Hj _]. apply Nat.ltb_lt in Hj.
    right. exists s, (ltree j). split; [reflexivity | exact (HP j Hj)].
  - apply IH. destruct u as [[s j]|]; [apply andb_prop in Hin as [_ H]; exact H | exact Hin].
Qed.

Lemma wf_states : forall j, j < n_l d -> wflttC (ltree j).
Proof.
  assert (H : forall T, (exists j, j < n_l d /\ T = ltree j) -> paco1 wfltt bot1 T).
  { pcofix CIH. intros T (j & Hj & ->). pfold.
    destruct (ltree_view j Hj) as (Hin & [(_ & ->) | [(_ & Hs & ->) | (_ & Hs & ->)]]).
    - apply wfltt_end.
    - apply wfltt_send; [exact (slist_lkid _ Hs)|].
      apply lkids_forall; [exact Hin|]. intros j' Hj'. right. apply CIH. exists j'. auto.
    - apply wfltt_recv; [exact (slist_lkid _ Hs)|].
      apply lkids_forall; [exact Hin|]. intros j' Hj'. right. apply CIH. exists j'. auto. }
  intros j Hj. apply H. exists j. auto.
Qed.

(* ── Projection ── *)

Definition mkid (k : nat) (u : option (sort * nat)) : option ltt :=
  match u with Some (_, g) => Some (ltree (proj_of d k g)) | None => None end.

Lemma kids_proj_forall2 : forall (R : gtt -> part -> ltt -> Prop) k gks lks,
  kids_proj d k gks lks = true -> kids_in (n_g d) gks = true ->
  (forall g, g < n_g d -> R (tree g) (part_at d k) (ltree (proj_of d k g))) ->
  Forall2 (fun u v => (u = None /\ v = None) \/
                      exists s g t, u = Some (s, g) /\ v = Some (s, t) /\ R g (part_at d k) t)
          (map gkid gks) (map lkid lks).
Proof.
  intros R k gks. induction gks as [|u gks IH]; intros lks Hp Hin HR.
  - destruct lks; [constructor | discriminate].
  - unfold kids_in in Hin; simpl in Hin.
    destruct u as [[s g]|]; destruct lks as [|[[s' j]|] lks]; simpl in Hp; try discriminate.
    + apply andb_prop in Hin as [Hg Hin]. apply Nat.ltb_lt in Hg.
      apply andb_prop in Hp as [Hp Hks]. apply andb_prop in Hp as [Hs Hj].
      apply sort_eqb_sound in Hs. apply Nat.eqb_eq in Hj. subst s' j.
      constructor; [|exact (IH lks Hks Hin HR)].
      right. exists s, (tree g), (ltree (proj_of d k g)).
      split; [reflexivity | split; [reflexivity | exact (HR g Hg)]].
    + constructor; [left; split; reflexivity | exact (IH lks Hp Hin HR)].
Qed.

Lemma merge_forall2 : forall (R : gtt -> part -> ltt -> Prop) k gks,
  kids_in (n_g d) gks = true ->
  (forall g, g < n_g d -> R (tree g) (part_at d k) (ltree (proj_of d k g))) ->
  Forall2 (fun u v => (u = None /\ v = None) \/
                      exists s g t, u = Some (s, g) /\ v = Some t /\ R g (part_at d k) t)
          (map gkid gks) (map (mkid k) gks).
Proof.
  intros R k gks. induction gks as [|u gks IH]; intros Hin HR; simpl; constructor;
    unfold kids_in in Hin; simpl in Hin.
  - destruct u as [[s g]|]; [|left; split; reflexivity].
    apply andb_prop in Hin as [Hg _]. apply Nat.ltb_lt in Hg.
    right. exists s, (tree g), (ltree (proj_of d k g)).
    split; [reflexivity | split; [reflexivity | exact (HR g Hg)]].
  - apply IH; [|exact HR]. destruct u as [[s g]|]; [apply andb_prop in Hin as [_ H]; exact H | exact Hin].
Qed.

Lemma merge_sound : forall k j gks, merge_ok d k j gks = true -> isMerge (ltree j) (map (mkid k) gks).
Proof.
  intros k j gks. induction gks as [|u gks IH]; intro H; [discriminate|].
  destruct u as [[s g]|].
  - destruct gks as [|u' gks].
    + simpl in H. apply Nat.eqb_eq in H. simpl. rewrite H. apply matm.
    + change ((Nat.eqb (proj_of d k g) j && merge_ok d k j (u' :: gks)) = true) in H.
      apply andb_prop in H as [Hg H]. apply Nat.eqb_eq in Hg.
      simpl. rewrite Hg. apply mconss. exact (IH H).
  - simpl. apply mconsn. exact (IH H).
Qed.

Lemma proj_states : forall k i, k < length (d_parts d) -> i < n_g d ->
  projectionC (tree i) (part_at d k) (ltree (proj_of d k i)).
Proof.
  assert (H : forall G r T, (exists k i, k < length (d_parts d) /\ i < n_g d /\ G = tree i /\
                                    r = part_at d k /\ T = ltree (proj_of d k i)) ->
                            paco3 projection bot3 G r T).
  { pcofix CIH. intros G rr T (k & i & Hk & Hi & -> & -> & ->). pfold.
    assert (HR : forall g, g < n_g d -> upaco3 projection r (tree g) (part_at d k) (ltree (proj_of d k g))).
    { intros g Hg. right. apply CIH. exists k, g. auto. }
    pose proof (Hl_proj k i Hk Hi) as Hp. unfold proj_ok in Hp.
    apply andb_prop in Hp as [Hj Hp]. apply Nat.ltb_lt in Hj.
    destruct (ltree_view _ Hj) as (_ & Hlv).
    destruct (rank_of d k i) as [r0|] eqn:Hr.
    2:{ apply Nat.eqb_eq in Hp. destruct Hlv as [(_ & ->) | [(Hk1 & _) | (Hk2 & _)]];
          [|rewrite Hp in Hk1; discriminate | rewrite Hp in Hk2; discriminate].
        apply proj_end. exact (part_neg k i Hk Hi Hr). }
    pose proof (part_pos k r0 i Hk Hi Hr) as Hpart.
    destruct (state_view i Hi) as (_ & _ & _ & Hkin & _).
    destruct (gs_head (gst d i)) as [[p q]|] eqn:Hh; [|discriminate].
    assert (Ht : tree i = gtt_send p q (map gkid (gs_kids (gst d i)))).
    { rewrite (tree_unf i Hi). unfold gshape. rewrite Hh. reflexivity. }
    rewrite Ht in Hpart |- *.
    destruct (Nat.eqb p (part_at d k)) eqn:Hpr.
    - apply Nat.eqb_eq in Hpr. rewrite <- Hpr in Hpart |- *.
      apply andb_prop in Hp as [Hp Hks]. apply andb_prop in Hp as [Hq Hkind].
      apply negb_true_iff in Hq. apply Nat.eqb_neq in Hq.
      unfold is_kind in Hkind. apply andb_prop in Hkind as [Hk1 Hpeer]. apply Nat.eqb_eq in Hk1, Hpeer.
      destruct Hlv as [(Hk0 & _) | [(_ & _ & ->) | (Hk2 & _)]]; [congruence | | congruence].
      rewrite Hpeer. apply proj_out; [intro He; apply Hq; rewrite <- He; exact Hpr | exact Hpart |].
      rewrite Hpr. exact (kids_proj_forall2 _ k _ _ Hks Hkin HR).
    - destruct (Nat.eqb q (part_at d k)) eqn:Hqr.
      + apply Nat.eqb_eq in Hqr. rewrite <- Hqr in Hpart |- *.
        apply Nat.eqb_neq in Hpr.
        apply andb_prop in Hp as [Hkind Hks].
        unfold is_kind in Hkind. apply andb_prop in Hkind as [Hk2 Hpeer]. apply Nat.eqb_eq in Hk2, Hpeer.
        destruct Hlv as [(Hk0 & _) | [(Hk1 & _) | (_ & _ & ->)]]; [congruence | congruence |].
        rewrite Hpeer. apply proj_in; [intro He; apply Hpr; rewrite He; exact Hqr | exact Hpart |].
        rewrite Hqr. exact (kids_proj_forall2 _ k _ _ Hks Hkin HR).
      + apply Nat.eqb_neq in Hpr, Hqr.
        apply andb_prop in Hp as [Hpq Hm]. apply negb_true_iff in Hpq. apply Nat.eqb_neq in Hpq.
        eapply proj_cont with (ys := map (mkid k) (gs_kids (gst d i)));
          [exact Hpq | intro He; apply Hqr; exact He | intro He; apply Hpr; exact He | exact Hpart
          | exact (merge_forall2 _ k _ Hkin HR) | exact (merge_sound k _ _ Hm)]. }
  intros k i Hk Hi. apply H. exists k, i. auto.
Qed.

Lemma projectable_root : projectableA (tree 0).
Proof.
  intro pt. destruct (in_parts d pt) eqn:Hin.
  - destruct (in_parts_index pt Hin) as (k & Hk & <-).
    exists (ltree (proj_of d k 0)). exact (proj_states k 0 Hk Hg_nonempty).
  - exists ltt_end. pfold. apply proj_end. exact (part_out pt 0 Hin Hg_nonempty).
Qed.

(* ── Subtyping ── *)

Lemma sendb_sound : forall (R : ltt -> ltt -> Prop) xs ys, sendb d xs ys = true ->
  (forall t t', In (t, t') (d_sub d) -> R (ltree t) (ltree t')) ->
  wfsend subsort R (map lkid xs) (map lkid ys).
Proof.
  intros R xs. induction xs as [|u xs IH]; intros ys Hb HR; [destruct ys; exact I|].
  destruct u as [[s t]|]; destruct ys as [|[[s' t']|] ys]; simpl in Hb |- *; try discriminate.
  - apply andb_prop in Hb as [Hb Hys]. apply andb_prop in Hb as [Hs Ht].
    split; [exact (subsortb_sound _ _ Hs)|]. split; [exact (HR t t' (memb_in _ _ _ Ht))|].
    exact (IH ys Hys HR).
  - exact (IH ys Hb HR).
  - exact (IH ys Hb HR).
Qed.

Lemma recvb_sound : forall (R : ltt -> ltt -> Prop) ys xs, recvb d ys xs = true ->
  (forall t t', In (t, t') (d_sub d) -> R (ltree t) (ltree t')) ->
  wfrec subsort R (map lkid ys) (map lkid xs).
Proof.
  intros R ys. induction ys as [|u ys IH]; intros xs Hb HR; [destruct xs; exact I|].
  destruct u as [[s' t']|]; destruct xs as [|[[s t]|] xs]; simpl in Hb |- *; try discriminate.
  - apply andb_prop in Hb as [Hb Hxs]. apply andb_prop in Hb as [Hs Ht].
    split; [exact (subsortb_sound _ _ Hs)|]. split; [exact (HR t t' (memb_in _ _ _ Ht))|].
    exact (IH xs Hxs HR).
  - exact (IH xs Hb HR).
  - exact (IH xs Hb HR).
Qed.

Lemma sub_states : forall a b, In (a, b) (d_sub d) -> subtypeC (ltree a) (ltree b).
Proof.
  assert (H : forall A B, (exists a b, In (a, b) (d_sub d) /\ A = ltree a /\ B = ltree b) ->
                          paco2 subtype bot2 A B).
  { pcofix CIH. intros A B (a & b & Hin & -> & ->). pfold.
    assert (HR : forall t t', In (t, t') (d_sub d) -> upaco2 subtype r (ltree t) (ltree t')).
    { intros t t' Ht. right. apply CIH. exists t, t'. auto. }
    pose proof (Hl_sub a b Hin) as Hs. unfold sub_ok in Hs.
    apply andb_prop in Hs as [Hs Hk]. apply andb_prop in Hs as [Ha Hb].
    apply Nat.ltb_lt in Ha, Hb.
    destruct (ltree_view a Ha) as (_ & [(Ka & ->) | [(Ka & _ & ->) | (Ka & _ & ->)]]);
    destruct (ltree_view b Hb) as (_ & [(Kb & ->) | [(Kb & _ & ->) | (Kb & _ & ->)]]);
    rewrite Ka, Kb in Hk; try discriminate.
    - apply sub_end.
    - apply andb_prop in Hk as [Hpeer Hk]. apply Nat.eqb_eq in Hpeer. rewrite Hpeer.
      apply sub_out. exact (sendb_sound _ _ _ Hk HR).
    - apply andb_prop in Hk as [Hpeer Hk]. apply Nat.eqb_eq in Hpeer. rewrite Hpeer.
      apply sub_in. exact (recvb_sound _ _ _ Hk HR). }
  intros a b Hin. apply H. exists a, b. auto.
Qed.

(* ── The context ── *)

Fixpoint build (es : list (nat * nat)) : tctx :=
  match es with
  | [] => M.empty
  | (p, j) :: es' => M.add p (ltree j) (build es')
  end.

Lemma find_build : forall es p, M.find p (build es) =
  match find (fun e => Nat.eqb (fst e) p) es with Some (_, j) => Some (ltree j) | None => None end.
Proof.
  induction es as [|[q j] es IH]; intro p; simpl.
  - apply M.empty_spec.
  - destruct (Nat.eqb q p) eqn:He.
    + apply Nat.eqb_eq in He. subst q. apply M.add_spec1.
    + apply Nat.eqb_neq in He. rewrite M.add_spec2; [apply IH | exact He].
Qed.

Lemma ctx_find : forall p T, M.find p (build (d_ctx d)) = Some T ->
  exists j, In (p, j) (d_ctx d) /\ T = ltree j.
Proof.
  intros p T H. rewrite find_build in H.
  destruct (find (fun e => Nat.eqb (fst e) p) (d_ctx d)) as [[q j]|] eqn:Hf; [|discriminate].
  injection H as <-. apply find_some in Hf as [Hin He]. simpl in He. apply Nat.eqb_eq in He. subst q.
  exists j. auto.
Qed.

Lemma ctx_present : forall k, k < length (d_parts d) ->
  exists T, M.find (part_at d k) (build (d_ctx d)) = Some T.
Proof.
  intros k Hk. rewrite find_build.
  destruct (find (fun e => Nat.eqb (fst e) (part_at d k)) (d_ctx d)) as [[q j]|] eqn:Hf; [eauto|].
  exfalso. pose proof (Hl_cover k Hk) as Hc. apply existsb_exists in Hc as ([q j] & Hin & He).
  pose proof (find_none _ _ Hf (q, j) Hin) as Hn. simpl in He, Hn. congruence.
Qed.

Lemma index_of_some : forall p l k, index_of p l = Some k -> k < length l /\ nth k l 0 = p.
Proof.
  intros p l. induction l as [|x l IH]; intros k H; simpl in H; [discriminate|].
  destruct (Nat.eqb p x) eqn:He.
  - injection H as <-. apply Nat.eqb_eq in He. simpl. split; [lia | auto].
  - destruct (index_of p l) as [k'|] eqn:Hi; simpl in H; [|discriminate].
    injection H as <-. destruct (IH k' eq_refl) as [Hk Hn]. simpl. split; [lia | exact Hn].
Qed.

Lemma index_of_none : forall p l, index_of p l = None -> existsb (Nat.eqb p) l = false.
Proof.
  intros p l. induction l as [|x l IH]; intro H; simpl in H |- *; [reflexivity|].
  destruct (Nat.eqb p x); [discriminate|]. simpl.
  destruct (index_of p l); [discriminate | exact (IH eq_refl)].
Qed.

Lemma ctx_entry : forall p j, In (p, j) (d_ctx d) ->
  j < n_l d /\
  ((exists k, k < length (d_parts d) /\ part_at d k = p /\ In (j, proj_of d k 0) (d_sub d)) \/
   (in_parts d p = false /\ ltree j = ltt_end)).
Proof.
  intros p j Hin. pose proof (Hl_entry _ Hin) as He. unfold ctx_entry_ok in He.
  apply andb_prop in He as [Hj He]. apply Nat.ltb_lt in Hj. split; [exact Hj|].
  destruct (index_of p (d_parts d)) as [k|] eqn:Hi.
  - left. destruct (index_of_some _ _ _ Hi) as [Hk Hn]. exists k.
    split; [exact Hk|]. split; [exact Hn | exact (memb_in _ _ _ He)].
  - right. split; [exact (index_of_none _ _ Hi)|]. apply Nat.eqb_eq in He.
    destruct (ltree_view j Hj) as (_ & [(_ & Hl) | [(Hk1 & _) | (Hk2 & _)]]); [exact Hl | congruence | congruence].
Qed.

Lemma wf_ctx : tctx_wf (build (d_ctx d)).
Proof.
  intros p T HT. destruct (ctx_find p T HT) as (j & Hinj & ->).
  exact (wf_states j (proj1 (ctx_entry p j Hinj))).
Qed.

Lemma assoc_ctx : assoc (build (d_ctx d)) (tree 0).
Proof.
  intro p. split.
  - intro Hp. destruct (in_parts d p) eqn:Hin.
    + destruct (in_parts_index p Hin) as (k & Hk & Hpk).
      destruct (ctx_present k Hk) as (T & HT). rewrite Hpk in HT.
      destruct (ctx_find p T HT) as (j & Hinj & ->).
      destruct (ctx_entry p j Hinj) as (_ & [(k' & Hk' & Hpk' & Hsub) | (Hn & _)]); [|congruence].
      exists (ltree j). split; [exact HT|].
      exists (ltree (proj_of d k' 0)). split.
      * rewrite <- Hpk'. exact (proj_states k' 0 Hk' Hg_nonempty).
      * exact (sub_states _ _ Hsub).
    + exfalso. exact (part_out p 0 Hin Hg_nonempty Hp).
  - intros Hn T HT. destruct (ctx_find p T HT) as (j & Hinj & ->).
    destruct (ctx_entry p j Hinj) as (_ & [(k & Hk & Hpk & _) | (_ & He)]); [|exact He].
    exfalso. apply Hn. rewrite <- Hpk. destruct (Hg_root k Hk) as (r & Hr).
    exact (part_pos k r 0 Hk Hg_nonempty Hr).
Qed.

Lemma live_in : liveCtx (build (d_ctx d)).
Proof.
  apply liveness with (g := tree 0).
  - exact (wfgC_states 0 Hg_nonempty).
  - exact projectable_root.
  - exact wf_ctx.
  - exact assoc_ctx.
Qed.

End Global.

(* The typing context of a certificate is live. *)
Theorem certificate_live : forall d tree ltree,
  (forall i, i < n_g d -> tree i = gshape d tree i) ->
  (forall j, j < n_l d -> ltree j = lshape d ltree j) ->
  global_ok d = true -> local_ok d = true ->
  liveCtx (build ltree (d_ctx d)).
Proof. intros d tree ltree Ht Hl Hg Hloc. eapply live_in; eauto. Qed.
